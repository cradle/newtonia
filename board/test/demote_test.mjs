// Protocol test for the retention DEMOTE pass against the real worker
// under wrangler dev (Workers review 2026-09-08, F5/F7), driven through
// wrangler's --test-scheduled hook like site_test.mjs. The unit test
// (retention_test.mjs) proves the pass on a real SQLite; this proves it on
// workerd's D1 + R2 with the protocol's own read paths:
//
//   102 accounts submit to one canonical season (one row each, the
//   one-row-per-player index) -> every row still carries its replay
//   the cron fires             -> the two rows ranked past KEEP_N (100)
//                                 lose their replay (WS fetch: no-replay,
//                                 site GET: 404) AND their objects are gone
//                                 from the bucket (both read paths refuse
//                                 on the cleared blob_key alone, so the
//                                 bucket is listed through wrangler's local
//                                 explorer API), the top 100 keep theirs,
//                                 and the site snapshot is republished
//                                 with the revision advanced (a demotion
//                                 is a score mutation)
//
// Run against `wrangler dev --local --test-scheduled --port 8790
//   --var FAKE_VERIFY:1 --var SUBMIT_LIMIT:250 --var CONN_LIMIT:500`
// (deploy-board.yml's "Site endpoints test" boot; the widened windows are
// dev-host-only and needed for the 102-account fill). Node 22+.
import { build_nrp } from "./nrp_fixture.mjs";

const WS_BASE = process.env.SITE_TEST_WS_URL || "ws://127.0.0.1:8790/board";
const HTTP_BASE = process.env.SITE_TEST_HTTP_URL || "http://127.0.0.1:8790";
const SEASON = "s71";     // canonical (the site boot runs the production whitelist)
const N = 102;            // KEEP_N + 2
const RUN0 = 710000n;

let failures = 0;
function check(name, cond, detail) {
  console.log((cond ? "PASS " : "FAIL ") + name +
              (cond || detail === undefined ? "" : `  (${detail})`));
  if (!cond) failures++;
}

function connect() {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(WS_BASE);
    const inbox = [], waiters = [];
    ws.recv = () => new Promise((r) => inbox.length ? r(inbox.shift()) : waiters.push(r));
    ws.onmessage = (m) => {
      const f = typeof m.data === "string" ? JSON.parse(m.data) : { t: "bin" };
      if (waiters.length) waiters.shift()(f); else inbox.push(f);
    };
    ws.onopen = () => resolve(ws);
    ws.onerror = () => reject(new Error(`ws error (${WS_BASE})`));
  });
}
const send = (ws, o) => ws.send(JSON.stringify(o));

async function submit(ws, bytes, name) {
  send(ws, { t: "submit", size: bytes.length, platform: 2, name, cred: "x" });
  const ok = await ws.recv();
  if (ok.t !== "submit-ok") return ok;
  for (let p = 0; p < bytes.length; p += 15 * 1024)
    ws.send(bytes.subarray(p, Math.min(p + 15 * 1024, bytes.length)));
  send(ws, { t: "submit-end" });
  return ws.recv();
}

// WS fetch: the first frame is fetch-ok (blob follows) or err no-replay.
async function has_replay_ws(run_id) {
  const ws = await connect();
  send(ws, { t: "fetch", season: SEASON, run_id: String(run_id) });
  const f = await ws.recv();
  if (f.t === "fetch-ok") {
    let g;
    do { g = await ws.recv(); } while (g.t !== "fetch-end" && g.t !== "err");
  }
  ws.close();
  return f.t === "fetch-ok";
}
async function replay_status(run_id) {
  const r = await fetch(`${HTTP_BASE}/replay/${SEASON}/${run_id}.nrp`);
  return r.status;
}
async function site() {
  return (await fetch(`${HTTP_BASE}/site/leaderboard.json`)).json();
}
// The bucket's keys under this season, via wrangler dev's local explorer
// API (`wrangler dev --local` only). Objects are keyed per upload
// (season/run_id-<nonce>.nrp), so a run's key is found by its prefix.
async function season_keys() {
  const keys = [];
  let cursor = "";
  for (;;) {
    const url = `${HTTP_BASE}/cdn-cgi/local/explorer/api/r2/buckets/newtonia-replays/objects` +
                `?prefix=${SEASON}/` + (cursor ? `&cursor=${encodeURIComponent(cursor)}` : "");
    const j = await (await fetch(url)).json();
    for (const o of j.result || []) if (o.key.startsWith(`${SEASON}/`)) keys.push(o.key);
    const info = j.result_info || {};
    if (String(info.is_truncated) !== "true") return keys;
    if (!info.cursor) throw new Error("bucket listing truncated with no cursor");
    cursor = info.cursor;
  }
}
const key_of = (keys, run_id) => keys.filter((k) => k.startsWith(`${SEASON}/${run_id}-`));

// ---- fill: 102 accounts, scores 1000..899, two submits per socket -------------
let placed = 0;
for (let i = 0; i < N; i += 2) {
  const ws = await connect();
  for (const k of [i, i + 1]) {
    if (k >= N) break;
    const blob = build_nrp({ game_version: SEASON, run_id: RUN0 + BigInt(k), score: 1000 - k });
    const f = await submit(ws, blob, `ACC${String(k).padStart(3, "0")}`);
    if (f.t === "placed") placed++;
    else console.log(`submit ${k}: ${JSON.stringify(f)}`);
  }
  ws.close();
}
check(`all ${N} submissions placed`, placed === N, `${placed}`);

const lowest = RUN0 + BigInt(N - 1), second_lowest = RUN0 + BigInt(N - 2);
const cut = RUN0 + BigInt(N - 3);  // rank 100: the last row to KEEP its replay
check("before cron: rank-101 row has its replay (WS)", await has_replay_ws(second_lowest));
check("before cron: rank-102 row has its replay (site)", await replay_status(lowest) === 200);
const before = await site();
const board_before = before.boards.find((b) => b.season === SEASON && b.players === 1);
check("before cron: site lists the board's top 100",
      !!board_before && board_before.rows.length === 100,
      board_before && `${board_before.rows.length}`);
const keys_before = await season_keys();
check(`before cron: the bucket holds one object per row (${N})`,
      keys_before.length === N, `${keys_before.length}`);
const [k_lowest] = key_of(keys_before, lowest);
const [k_second] = key_of(keys_before, second_lowest);
const [k_cut] = key_of(keys_before, cut);
check("before cron: the three probed rows each have exactly one object",
      key_of(keys_before, lowest).length === 1 && key_of(keys_before, second_lowest).length === 1 &&
      key_of(keys_before, cut).length === 1);

// ---- the cron ---------------------------------------------------------------------
const r = await fetch(`${HTTP_BASE}/__scheduled?cron=17+4+*+*+*`);
check("cron fired through --test-scheduled", r.ok, `HTTP ${r.status}`);

// ---- after: rows past the cut are score-only, the rest keep their replay ----------
check("after cron: rank-101 row lost its replay (WS no-replay)",
      !(await has_replay_ws(second_lowest)));
check("after cron: rank-102 row lost its replay (site 404)",
      await replay_status(lowest) === 404);
check("after cron: rank-100 row keeps its replay (site 200)",
      await replay_status(cut) === 200);
check("after cron: rank-1 row keeps its replay (WS)", await has_replay_ws(RUN0));
const keys_after = await season_keys();
check("after cron: the demoted rows' objects are deleted from the bucket",
      !keys_after.includes(k_lowest) && !keys_after.includes(k_second),
      `${k_lowest}: ${keys_after.includes(k_lowest)}, ${k_second}: ${keys_after.includes(k_second)}`);
check("after cron: the rank-100 row's object is still in the bucket",
      keys_after.includes(k_cut), k_cut);
check("after cron: the bucket holds exactly the top 100 objects",
      keys_after.length === N - 2, `${keys_after.length}`);
const after = await site();
const board_after = after.boards.find((b) => b.season === SEASON && b.players === 1);
check("after cron: site still lists the top 100, all with replays",
      !!board_after && board_after.rows.length === 100 &&
      board_after.rows.every((x) => x.has_replay === true));
check("after cron: the snapshot revision advanced (a demotion is a mutation)",
      typeof after.rev === "number" && after.rev > before.rev,
      `${before.rev} -> ${after.rev}`);

console.log(failures ? `${failures} FAILURE(S)` : "DEMOTE-OK");
process.exit(failures ? 1 : 0);
