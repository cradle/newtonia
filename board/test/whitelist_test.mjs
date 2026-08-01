// Production season-whitelist test (CANONICAL_SEASONS_ONLY=1 — the
// top-level wrangler.toml default): non-canonical seasons never qualify
// and are refused at admission; canonical seasons pass. Run against
// `wrangler dev --local --var FAKE_VERIFY:1 --var SUBMIT_LIMIT:100`
// WITHOUT the CANONICAL_SEASONS_ONLY:0 opt-out the main protocol test uses.
import { build_nrp } from "./nrp_fixture.mjs";

const BASE = process.env.BOARD_TEST_URL || "ws://127.0.0.1:8787/board";

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
    const inbox = [], waiters = [];
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

async function submit(ws, bytes, name) {
  send(ws, { t: "submit", size: bytes.length, platform: 2, name, cred: "x" });
  const ok = await ws._recv();
  if (ok.t !== "submit-ok") return ok;
  for (let p = 0; p < bytes.length; p += 15000)
    ws.send(bytes.subarray(p, Math.min(p + 15000, bytes.length)));
  send(ws, { t: "submit-end" });
  return ws._recv();
}

(async () => {
  const ws = await connect();

  // Non-canonical season: never qualifies (the game-over prompt on a dev
  // build pointed at production stays silent)...
  send(ws, { t: "qualify", season: "vtest-dev", players: 1, score: 999 });
  let f = await ws._recv();
  check("non-canonical never qualifies",
        f.t === "qualify" && f.would_place === false, JSON.stringify(f));

  // ...and is refused at admission even if submitted anyway.
  const dev = build_nrp({ game_version: "vtest-dev", run_id: 1n, score: 999 });
  f = await submit(ws, dev, "DEV");
  check("non-canonical submit refused bad-season",
        f.t === "err" && f.reason === "bad-season", JSON.stringify(f));

  // Canonical season: qualifies and places.
  send(ws, { t: "qualify", season: "s98", players: 1, score: 500 });
  f = await ws._recv();
  check("canonical qualifies", f.t === "qualify" && f.would_place === true,
        JSON.stringify(f));
  const canon = build_nrp({ game_version: "s98", run_id: 2n, score: 500 });
  f = await submit(ws, canon, "CANON");
  check("canonical submit placed", f.t === "placed" && f.rank === 1,
        JSON.stringify(f));

  ws.close();
  console.log(failures ? `${failures} FAILURE(S)` : "ALL PASS");
  process.exit(failures ? 1 : 0);
})();
