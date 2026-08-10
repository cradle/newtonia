// PB-D5 family 3 — offer buffering rules: addressed offers are RELAY-ONLY
// (a jid's socket is connected for its whole life — the host can only
// learn a jid from its join event — so a disconnected target means the
// jid departed and the offer is stale; storing it would only grow the
// room record). The legacy unaddressed slot is the one buffer that
// exists, replayed once to the next arrival and consumed on delivery —
// a second joiner must never receive the same single-use SDP.
import { t, check, finish, host_room, join_room } from "./ws_harness.mjs";

(async () => {
  // Legacy buffer: offer + cands stored before any joiner, replayed in
  // order to the first arrival.
  const { host, code } = await host_room();
  host.send(JSON.stringify({ t: "offer", sdp: "EARLY-SDP", pv: "25" }));
  host.send(JSON.stringify({ t: "cand", mid: "0", cand: "EARLY-C1" }));
  host.send(JSON.stringify({ t: "cand", mid: "0", cand: "EARLY-C2" }));
  await t(200);
  const j1 = await join_room(code);
  const o = await j1._recvType("offer");
  check("first joiner got the buffered offer", o && o.sdp === "EARLY-SDP" && o.pv === "25");
  const c1 = await j1._recvType("cand");
  const c2 = await j1._recvType("cand");
  check("buffered cands replayed in order",
        c1 && c1.cand === "EARLY-C1" && c2 && c2.cand === "EARLY-C2");

  // Consumed: a second joiner gets NO replay of the same SDP.
  const j2 = await join_room(code);
  await t(250);
  check("second joiner did not receive the consumed offer",
        !j2._drain().some((f) => f && f.t === "offer"));

  // Addressed offers are relay-only: one sent to a DEPARTED jid is
  // dropped outright — neither stored for a fresh joiner nor misdelivered
  // to a sitting one.
  const id2 = "2";  // second join in a fresh room mints jid 2
  j2.close();
  await t(250);
  host.send(JSON.stringify({ t: "offer", sdp: "FOR-GONE-J2", to: id2 }));
  await t(200);
  check("offer to a departed jid not misdelivered to a sitting joiner",
        !j1._drain().some((f) => f && f.t === "offer"));
  const j3 = await join_room(code);
  await t(250);
  check("a fresh joiner does not inherit a departed jid's offer",
        !j3._drain().some((f) => f && f.t === "offer"));

  host.close(); j1.close(); j3.close();
  finish("JID-BUFFER-TEST-OK");
})();
