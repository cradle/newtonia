// Manual tool: submit ONE run several times at different scores and watch
// what the board does with the row, the object and the site snapshot.
// The game client can never produce this by itself — a resumed run only
// goes up, and the LEADERBOARD screen hides UPLOAD BEST RUN whenever the
// board already carries the run at or above the local score — so the
// worker-side rules (LEADERBOARD.md "Workers review 2026-09-08") are only
// reachable through the raw protocol:
//
//   500  -> placed; one object under season/run_id-<nonce>.nrp
//   300  -> err already-submitted, refused at the pre-check BEFORE any
//           object is written: the bucket and the site snapshot are
//           unchanged, the revision does not move
//   700  -> placed; a NEW object (new nonce), the earlier one deleted; the
//           site snapshot shows 700 on the very next view (the revision
//           counter, not a timestamp or the cron, is what makes it stale)
//
// Needs the board worker running LOCALLY with fake attestation — a real
// deployment only accepts a genuine platform credential:
//
//   cd board
//   npx wrangler@4 dev --local --port 8788 --persist-to .wrangler-manual \
//       --var FAKE_VERIFY:1 --var SUBMIT_LIMIT:250 --var CONN_LIMIT:500
//   node test/manual_resubmit.mjs                 # synthetic fixture run
//   node test/manual_resubmit.mjs path/to/best.nrp   # your own replay
//   node test/manual_resubmit.mjs best.nrp 500 300 700 1200   # your scores
//   BOARD_NAME=OTHER node test/manual_resubmit.mjs "" 900   # another account,
//                                  same run_id -> already-submitted, no write
//
// With a real replay the score is patched in a COPY of its header each
// time (little-endian u32 at byte 52 — replay.h's Header layout, the same
// field the fixture writes); the file on disk is never touched. The bucket
// listing comes from wrangler's local explorer API, so it only works
// against `wrangler dev --local`. Node 22+ (global WebSocket + fetch).
import { readFileSync } from "node:fs";
import { build_nrp } from "./nrp_fixture.mjs";

const PORT = process.env.BOARD_PORT || "8788";
const WS = `ws://127.0.0.1:${PORT}/board`;
const HTTP = `http://127.0.0.1:${PORT}`;
const SEASON = process.env.BOARD_SEASON || "s1";
// The submitting "account": FAKE_VERIFY derives it from the name, so a
// second run with BOARD_NAME=OTHER is another player colliding on the same
// run_id — refused as already-submitted, nothing written.
const NAME = process.env.BOARD_NAME || "MANUAL";

const [, , file, ...score_args] = process.argv;
const real = file ? new Uint8Array(readFileSync(file)) : null;
const scores = score_args.length ? score_args.map(Number) : [500, 300, 700];

function with_score(score) {
  const b = real ? new Uint8Array(real)
                 : build_nrp({ game_version: SEASON, run_id: 424242n, score });
  new DataView(b.buffer, b.byteOffset, b.byteLength).setUint32(52, score, true);
  return b;
}

function connect() {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(WS);
    const inbox = [], waiters = [];
    ws.recv = () => new Promise((r) => inbox.length ? r(inbox.shift()) : waiters.push(r));
    ws.onmessage = (m) => {
      const f = JSON.parse(m.data);
      if (waiters.length) waiters.shift()(f); else inbox.push(f);
    };
    ws.onopen = () => resolve(ws);
    ws.onerror = () => reject(new Error(`cannot reach ${WS} — is wrangler dev up?`));
  });
}

// One submission on a fresh socket (a connection allows two submits; a
// fresh one per step keeps the per-connection budget out of the picture).
async function submit(bytes) {
  const ws = await connect();
  ws.send(JSON.stringify({ t: "submit", size: bytes.length, platform: 2,
                           name: NAME, cred: "manual-cred" }));
  const ok = await ws.recv();
  if (ok.t !== "submit-ok") { ws.close(); return ok; }
  const CHUNK = 15 * 1024;
  for (let p = 0; p < bytes.length; p += CHUNK)
    ws.send(bytes.subarray(p, Math.min(p + CHUNK, bytes.length)));
  ws.send(JSON.stringify({ t: "submit-end" }));
  const reply = await ws.recv();
  ws.close();
  return reply;
}

async function site() {
  const r = await fetch(`${HTTP}/site/leaderboard.json`);
  if (!r.ok) return { status: r.status };
  const snap = await r.json();
  return { rev: snap.rev, boards: snap.boards.map((b) =>
      `${b.season}/${b.players}: ${b.rows.map((x) => x.score).join(",")}`) };
}

async function bucket() {
  try {
    const r = await fetch(`${HTTP}/cdn-cgi/local/explorer/api/r2/buckets/newtonia-replays/objects`);
    const j = await r.json();
    return (j.result || []).map((o) => o.key || o.name || JSON.stringify(o))
        .filter((k) => k !== "site/leaderboard.json");
  } catch (e) {
    return [`(bucket listing unavailable: ${e.message})`];
  }
}

console.log(`run: ${real ? file : "synthetic fixture"}  season: ${SEASON}  scores: ${scores.join(" -> ")}`);
console.log(`before: site=${JSON.stringify(await site())} objects=${JSON.stringify(await bucket())}`);
for (const score of scores) {
  const reply = await submit(with_score(score));
  console.log(`\nsubmit ${score}: ${JSON.stringify(reply)}`);
  console.log(`  site:    ${JSON.stringify(await site())}`);
  console.log(`  objects: ${JSON.stringify(await bucket())}`);
}
