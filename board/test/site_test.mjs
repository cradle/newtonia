// Protocol test for the website's read endpoints (LEADERBOARD.md "site
// leaderboard"): GET /site/leaderboard.json (the daily snapshot the Pages
// /leaderboard/ page renders) and GET /replay/<season>/<run_id>.nrp (the
// blob the /play/?replay= watch deep link fetches). Rows are seeded through
// the ordinary WS submit flow with FAKE_VERIFY attestation, then the cron
// refresh is driven through wrangler's --test-scheduled endpoint.
//
// Run against
//   wrangler dev --local --test-scheduled --port 8790 --var FAKE_VERIFY:1
// (production-default season whitelist — this test uses canonical seasons):
//   node test/site_test.mjs
import { build_nrp } from "./nrp_fixture.mjs";

const WS_BASE = process.env.SITE_TEST_WS_URL || "ws://127.0.0.1:8790/board";
const HTTP_BASE = process.env.SITE_TEST_HTTP_URL || "http://127.0.0.1:8790";
// Canonical (whitelisted) but far from real seasons; the local D1 simulator
// starts empty each CI boot, so collisions only matter for local re-runs —
// where the upsert semantics keep this test stable anyway.
const SEASON = "s93";

let failures = 0;
function check(name, cond, detail) {
  console.log((cond ? "PASS " : "FAIL ") + name +
              (cond || detail === undefined ? "" : `  (${detail})`));
  if (!cond) failures++;
}

function connect() {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(WS_BASE);
    ws.binaryType = "arraybuffer";
    const inbox = [];
    const waiters = [];
    ws._recv = () => new Promise((res, rej) => {
      if (inbox.length) return res(inbox.shift());
      waiters.push(res);
      setTimeout(() => rej(new Error("recv timeout")), 15000);
    });
    ws.onmessage = (m) => {
      const f = typeof m.data === "string" ? JSON.parse(m.data)
                                           : { t: "_bin", data: m.data };
      if (waiters.length) waiters.shift()(f);
      else inbox.push(f);
    };
    ws.onopen = () => resolve(ws);
    ws.onerror = () => reject(new Error("ws error"));
  });
}

function send(ws, obj) { ws.send(JSON.stringify(obj)); }

async function submit(bytes, name) {
  const ws = await connect();
  send(ws, { t: "submit", size: bytes.length, platform: 2, name, cred: "x" });
  const ok = await ws._recv();
  if (ok.t !== "submit-ok") { ws.close(); return ok; }
  for (let p = 0; p < bytes.length; p += 60000)
    ws.send(bytes.subarray(p, Math.min(p + 60000, bytes.length)));
  send(ws, { t: "submit-end" });
  const fin = await ws._recv();
  ws.close();
  return fin;
}

// Fire the worker's scheduled() through wrangler's --test-scheduled hook.
async function run_cron() {
  const r = await fetch(`${HTTP_BASE}/__scheduled?cron=17+4+*+*+*`);
  if (!r.ok) throw new Error(`__scheduled: HTTP ${r.status}`);
}

function board_of(snap, season, players) {
  return (snap.boards || []).find(
      (b) => b.season === season && b.players === players);
}

(async () => {
  // Seed two solo rows and one co-op row through the ordinary submit flow.
  const soloA = build_nrp({ game_version: SEASON, run_id: 7001n, score: 500 });
  const soloB = build_nrp({ game_version: SEASON, run_id: 7002n, score: 900 });
  const coop = build_nrp({ game_version: SEASON, run_id: 7003n, score: 700,
                           player_count: 2 });
  check("solo A placed", (await submit(soloA, "ALPHA")).t === "placed");
  check("solo B placed", (await submit(soloB, "BRAVO")).t === "placed");
  check("co-op placed", (await submit(coop, "ALPHA")).t === "placed");

  // Cold snapshot miss: built from D1 on the spot, stored, and served with
  // CORS + cache headers.
  let r = await fetch(`${HTTP_BASE}/site/leaderboard.json`);
  check("snapshot GET ok", r.status === 200, `HTTP ${r.status}`);
  check("snapshot CORS *",
        r.headers.get("Access-Control-Allow-Origin") === "*");
  check("snapshot content-type json",
        (r.headers.get("Content-Type") || "").includes("application/json"));
  check("snapshot cacheable",
        (r.headers.get("Cache-Control") || "").includes("max-age"));
  let snap = await r.json();
  check("snapshot has generated_at", typeof snap.generated_at === "number");
  let solo = board_of(snap, SEASON, 1);
  check("solo board present", !!solo, JSON.stringify(snap.boards &&
        snap.boards.map((b) => `${b.season}/${b.players}`)));
  check("solo rows ranked", !!solo && solo.rows.length >= 2 &&
        solo.rows[0].score === 900 && solo.rows[0].rank === 1 &&
        solo.rows[0].name === "BRAVO",
        solo && JSON.stringify(solo.rows.slice(0, 2)));
  check("solo rows carry watch fields", !!solo &&
        solo.rows[0].has_replay === true &&
        typeof solo.rows[0].run_id === "string");
  const coopB = board_of(snap, SEASON, 2);
  check("co-op board present with its row", !!coopB &&
        coopB.rows.length >= 1 && coopB.rows[0].score === 700);

  // Freshness: a submission newer than the stored snapshot triggers a
  // rebuild on the next read — new scores must not hide until the cron.
  const prev_generated = snap.generated_at;
  const soloC = build_nrp({ game_version: SEASON, run_id: 7004n, score: 950 });
  check("solo C placed", (await submit(soloC, "CHARLIE")).t === "placed");
  snap = await (await fetch(`${HTTP_BASE}/site/leaderboard.json`)).json();
  solo = board_of(snap, SEASON, 1);
  check("snapshot refreshed by newer submission",
        !!solo && solo.rows[0].score === 950 && solo.rows[0].name === "CHARLIE",
        solo && JSON.stringify(solo.rows[0]));
  check("refresh advanced generated_at", snap.generated_at > prev_generated,
        `${prev_generated} -> ${snap.generated_at}`);
  // A read with nothing new serves the stored snapshot unchanged.
  const again = await (await fetch(`${HTTP_BASE}/site/leaderboard.json`)).json();
  check("no rebuild without a newer submission",
        again.generated_at === snap.generated_at,
        `${snap.generated_at} -> ${again.generated_at}`);
  // The daily cron still republishes (retention demotions change data
  // without a new submission, so the cron write matters).
  await run_cron();
  const post = await (await fetch(`${HTTP_BASE}/site/leaderboard.json`)).json();
  check("cron republished snapshot", post.generated_at > snap.generated_at,
        `${snap.generated_at} -> ${post.generated_at}`);
  solo = board_of(post, SEASON, 1);
  check("cron snapshot content intact", !!solo && solo.rows[0].score === 950);
  check("live flag set on a listed board",
        (post.boards || []).some((b) => b.players === 1 && b.live === true));

  // Replay blob download: byte-identical to the submission, CORS'd, and
  // 404 on anything that isn't a charting row's blob.
  r = await fetch(`${HTTP_BASE}/replay/${SEASON}/7002.nrp`);
  check("replay GET ok", r.status === 200, `HTTP ${r.status}`);
  check("replay CORS *",
        r.headers.get("Access-Control-Allow-Origin") === "*");
  const bytes = new Uint8Array(await r.arrayBuffer());
  check("replay bytes match submission", bytes.length === soloB.length &&
        bytes.every((b, i) => b === soloB[i]), `${bytes.length} bytes`);
  r = await fetch(`${HTTP_BASE}/replay/${SEASON}/999999.nrp`);
  check("unknown run 404", r.status === 404, `HTTP ${r.status}`);
  r = await fetch(`${HTTP_BASE}/replay/${SEASON}/7002.nrp`,
                  { method: "POST" });
  check("non-GET refused", r.status === 405, `HTTP ${r.status}`);
  r = await fetch(`${HTTP_BASE}/replay/no%2Fsuch/1.nrp`);
  check("bad season 404", r.status === 404, `HTTP ${r.status}`);

  console.log(failures ? `${failures} FAILURE(S)` : "ALL PASS");
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.log("FATAL " + e.message); process.exit(1); });
