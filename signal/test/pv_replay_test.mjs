// Worker offer_pv persistence test: a version-stamped offer stored
// BEFORE the joiner arrives must be replayed WITH the stamp (the game
// fails fast on its absence — a worker that strips it false-fails every
// normal pairing). Run: node pv_replay_test.mjs  (wrangler dev on :8787;
// SIGNAL_WS overrides, e.g. the production relay).
const BASE = process.env.SIGNAL_WS || "ws://127.0.0.1:8787/ws";
const host = new WebSocket(BASE + "?role=host");
host.onmessage = (m) => {
  const f = JSON.parse(m.data);
  if (f.t === "room") {
    host.send(JSON.stringify({ t: "offer", pv: "10", sdp: "v=0\r\no=pvtest" }));
    setTimeout(() => {
      const join = new WebSocket(BASE + "?role=join&code=" + f.code);
      join.onmessage = (m2) => {
        const g = JSON.parse(m2.data);
        if (g.t === "offer") {
          const ok = g.pv === "10" && g.sdp === "v=0\r\no=pvtest";
          console.log((ok ? "PASS" : "FAIL") + " stored-offer replay keeps pv");
          host.send(JSON.stringify({ t: "close" }));
          console.log(ok ? "PV-REPLAY-TEST-OK" : "PV-REPLAY-TEST-FAIL");
          process.exit(ok ? 0 : 1);
        }
      };
    }, 1200);
  }
};
setTimeout(() => { console.log("TIMEOUT"); process.exit(1); }, 20000);
