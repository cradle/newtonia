// Protocol test for the leaderboard worker (LEADERBOARD.md L1): drives
// qualify/submit/top/rank-of/fetch end to end against the real worker code
// under miniflare, with FAKE_VERIFY standing in for platform attestation
// and a synthetic .nrp built by the same fixture the unit test uses.
//
// Run against `wrangler dev --local --port 8788 --var FAKE_VERIFY:1`:
//   node test/board_test.mjs
import { build_nrp, build_record, concat, build_header }
  from "./nrp_fixture.mjs";
import { FLAG_CLEAN, FLAG_CHEATED, REC_KEYFRAME, REC_DELTA }
  from "../src/validate.js";

const BASE = process.env.BOARD_TEST_URL || "ws://127.0.0.1:8788/board";
const SEASON = "vtest-" + Date.now().toString(36); // fresh board per run

let failures = 0;
function check(name, cond, detail) {
  console.log((cond ? "PASS " : "FAIL ") + name +
              (cond || detail === undefined ? "" : `  (${detail})`));
  if (!cond) failures++;
}

function connect() {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(BASE);
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

// Upload `bytes` through the submit flow; returns the final frame
// ({t:"placed"} or {t:"err"}).
async function submit(ws, bytes, name, platform = 2) {
  send(ws, { t: "submit", size: bytes.length, platform, name, cred: "x" });
  const ok = await ws._recv();
  if (ok.t !== "submit-ok") return ok;
  for (let p = 0; p < bytes.length; p += 60000)
    ws.send(bytes.subarray(p, Math.min(p + 60000, bytes.length)));
  send(ws, { t: "submit-end" });
  return ws._recv();
}

(async () => {
  const ws = await connect();

  // 1. Empty board: any score would place first, no cutline yet.
  send(ws, { t: "qualify", season: SEASON, players: 1, score: 100 });
  let f = await ws._recv();
  check("qualify empty board", f.t === "qualify" && f.place === 1 &&
        f.cutline === null && f.would_place === true, JSON.stringify(f));

  // 2. First submission places #1.
  const runA = build_nrp({ game_version: SEASON, run_id: 1001n, score: 500 });
  f = await submit(ws, runA, "ALICE");
  check("first submit placed #1", f.t === "placed" && f.rank === 1,
        JSON.stringify(f));

  // 3. A better run by a second player takes #1.
  const runB = build_nrp({ game_version: SEASON, run_id: 1002n, score: 900 });
  f = await submit(ws, runB, "BOB");
  check("better submit takes #1", f.t === "placed" && f.rank === 1,
        JSON.stringify(f));

  // 4. top lists both, ordered, with replays attached.
  send(ws, { t: "top", season: SEASON, players: 1, count: 10 });
  f = await ws._recv();
  check("top has both rows", f.t === "top" && f.rows.length === 2,
        JSON.stringify(f));
  check("top ordered by score", f.rows[0].name === "BOB" &&
        f.rows[0].rank === 1 && f.rows[1].name === "ALICE");
  check("rows carry replays", f.rows.every((r) => r.has_replay === true));
  check("rows verified", f.rows.every((r) => r.verified === true));

  // 5. rank-of between the two scores.
  send(ws, { t: "rank-of", season: SEASON, players: 1, score: 700 });
  f = await ws._recv();
  check("rank-of between rows", f.t === "rank-of" && f.place === 2,
        JSON.stringify(f));

  // 6. Same run resubmitted, no improvement: refused. Fresh socket — the
  // per-connection submit budget is 2 (checked explicitly at the end).
  const ws2 = await connect();
  f = await submit(ws2, runA, "ALICE");
  check("same run refused", f.t === "err" && f.reason === "already-submitted",
        JSON.stringify(f));

  // 7. Alice improves with a NEW run: her old row is superseded, not added.
  const runA2 = build_nrp({ game_version: SEASON, run_id: 1003n, score: 950 });
  f = await submit(ws2, runA2, "ALICE");
  check("improved run placed #1", f.t === "placed" && f.rank === 1,
        JSON.stringify(f));
  send(ws2, { t: "top", season: SEASON, players: 1, count: 10 });
  f = await ws2._recv();
  check("old personal row superseded", f.rows.length === 2 &&
        f.rows[0].name === "ALICE" && f.rows[1].name === "BOB",
        JSON.stringify(f.rows));
  const winner_run = f.rows[0].run_id;

  // 8. A WORSE run by an existing player: refused as not-best.
  const ws3 = await connect();
  const runA3 = build_nrp({ game_version: SEASON, run_id: 1004n, score: 200 });
  f = await submit(ws3, runA3, "ALICE");
  check("worse personal run refused", f.t === "err" && f.reason === "not-best",
        JSON.stringify(f));

  // 9. Rejections: cheat flag, bad framing, oversize announcement.
  const cheat = build_nrp({ game_version: SEASON, run_id: 1005n,
                            flags: FLAG_CLEAN | FLAG_CHEATED });
  f = await submit(ws3, cheat, "MALLORY");
  check("cheated refused", f.t === "err" && f.reason === "cheated",
        JSON.stringify(f));
  const ws4 = await connect();
  const noKey = concat(build_header({ game_version: SEASON, run_id: 1006n }),
                       build_record(0, REC_DELTA, 10),
                       build_record(1, REC_KEYFRAME, 10));
  f = await submit(ws4, noKey, "MALLORY");
  check("delta-first refused", f.t === "err" &&
        f.reason === "no-leading-keyframe", JSON.stringify(f));
  send(ws4, { t: "submit", size: 33 * 1024 * 1024, platform: 2,
              name: "MALLORY", cred: "x" });
  f = await ws4._recv();
  check("oversize announcement refused", f.t === "err" &&
        f.reason === "too-large", JSON.stringify(f));

  // 9b. Per-connection submit budget: every submit announcement consumes
  // a slot (noKey, then the oversize refusal), so the 3rd on this socket
  // is refused and the socket closed.
  f = await submit(ws4, runA3, "MALLORY");
  check("per-conn submit budget", f.t === "err" && f.reason === "rate-limited",
        JSON.stringify(f));

  // 10. Download the winner's replay and compare bytes.
  send(ws2, { t: "fetch", season: SEASON, run_id: winner_run });
  f = await ws2._recv();
  check("fetch-ok with size", f.t === "fetch-ok" && f.size === runA2.length,
        JSON.stringify(f));
  const got = new Uint8Array(f.size);
  let off = 0;
  while (off < f.size) {
    const c = await ws2._recv();
    if (c.t !== "_bin") { check("fetch stream binary", false, c.t); break; }
    got.set(new Uint8Array(c.data), off);
    off += c.data.byteLength;
  }
  f = await ws2._recv();
  check("fetch-end after chunks", f.t === "fetch-end", JSON.stringify(f));
  check("fetched bytes identical",
        got.length === runA2.length && got.every((b, i) => b === runA2[i]));

  // 11. Unknown run: no-replay.
  send(ws2, { t: "fetch", season: SEASON, run_id: "424242" });
  f = await ws2._recv();
  check("unknown fetch refused", f.t === "err" && f.reason === "no-replay",
        JSON.stringify(f));

  // 12. Co-op board is separate: the solo rows don't show under players=2.
  send(ws2, { t: "top", season: SEASON, players: 2, count: 10 });
  f = await ws2._recv();
  check("co-op board separate", f.t === "top" && f.rows.length === 0,
        JSON.stringify(f));

  // 13. rank-of / qualify echo the players field (stale-answer drop, and
  // ties project as ABOVE a not-yet-submitted run — the projected_rank
  // change). Board has ALICE 950 and BOB 900; a 900 tie projects to #3
  // (both existing >= 900 count), not the optimistic #2.
  send(ws2, { t: "rank-of", season: SEASON, players: 1, score: 900 });
  f = await ws2._recv();
  check("rank-of echoes players", f.t === "rank-of" && f.players === 1,
        JSON.stringify(f));
  check("tie projects below existing", f.place === 3, JSON.stringify(f));
  send(ws2, { t: "qualify", season: SEASON, players: 1, score: 900 });
  f = await ws2._recv();
  check("qualify echoes players", f.players === 1, JSON.stringify(f));

  // 14. One row per player is DB-enforced: a fresh socket submitting a
  // DIFFERENT run for ALICE's account (FAKE_VERIFY keys the account off
  // the name) with a higher score supersedes atomically — still one ALICE
  // row, and the old blob is gone.
  const ws5 = await connect();
  const runA4 = build_nrp({ game_version: SEASON, run_id: 2001n, score: 1200 });
  f = await submit(ws5, runA4, "ALICE");
  check("atomic supersede placed #1", f.t === "placed" && f.rank === 1,
        JSON.stringify(f));
  send(ws5, { t: "top", season: SEASON, players: 1, count: 10 });
  f = await ws5._recv();
  const aliceRows = f.rows.filter((r) => r.name === "ALICE");
  check("still one ALICE row after supersede", aliceRows.length === 1 &&
        aliceRows[0].score === 1200, JSON.stringify(f.rows));

  ws.close(); ws2.close(); ws3.close(); ws5.close();
  console.log(failures ? `${failures} FAILURE(S)` : "ALL PASS");
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.log("FAIL (exception) " + e.message); process.exit(1); });
