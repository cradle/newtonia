// PB-D5 family 3 — per-jid buffers: an offer (and trickle cands) addressed
// to a joiner that DROPPED are held in that jid's slot and replayed in
// order if the same... no — jids are never reused, so a dropped jid's
// buffer dies with it (drop_joiner deletes the slot); what IS replayed is
// the legacy slot to the next arrival, once, consumed on delivery. This
// family pins both: per-jid buffering while the target is briefly absent
// is impossible by construction (an addressed offer can only name a live
// jid the host learned from a join event), and the legacy one-shot replay
// is consumed by the FIRST arrival — a second joiner must never receive
// the same single-use SDP.
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

  // A dropped jid's addressed slot dies with it: offer to j2's id, drop
  // j2, a NEW joiner (fresh id) must not inherit the buffer.
  const id2 = "2";  // second join in a fresh room mints jid 2
  host.send(JSON.stringify({ t: "offer", sdp: "FOR-J2", to: id2 }));
  await t(150);
  j2.close();
  await t(250);
  const j3 = await join_room(code);
  await t(250);
  check("a fresh joiner does not inherit a dropped jid's offer",
        !j3._drain().some((f) => f && f.t === "offer"));

  host.close(); j1.close(); j3.close();
  finish("JID-BUFFER-TEST-OK");
})();
