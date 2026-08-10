// PB-D5 family 2 — per-jid relay isolation: an offer addressed {to: j1}
// reaches ONLY j1; j1's answer/cands reach the host stamped {from: j1};
// another joiner's frames carry ITS id. An unaddressed (legacy 2P) offer
// goes to the oldest connected joiner only.
import { t, check, finish, host_room, join_room } from "./ws_harness.mjs";

(async () => {
  const { host, code } = await host_room();
  const j1 = await join_room(code);
  const id1 = (await host._recvType("peer")).from;
  const j2 = await join_room(code);
  const id2 = (await host._recvType("peer")).from;

  // Addressed offers: each joiner sees only its own.
  host.send(JSON.stringify({ t: "offer", sdp: "SDP-FOR-1", pv: "25", to: id1 }));
  host.send(JSON.stringify({ t: "offer", sdp: "SDP-FOR-2", pv: "25", to: id2 }));
  const o1 = await j1._recvType("offer");
  const o2 = await j2._recvType("offer");
  check("j1 got its addressed offer (pv rides)", o1 && o1.sdp === "SDP-FOR-1" && o1.pv === "25");
  check("j2 got its addressed offer", o2 && o2.sdp === "SDP-FOR-2");
  await t(200);
  check("no cross-delivery to j1", !j1._drain().some((f) => f && f.t === "offer"));
  check("no cross-delivery to j2", !j2._drain().some((f) => f && f.t === "offer"));

  // Addressed cands: only the target sees them.
  host.send(JSON.stringify({ t: "cand", mid: "0", cand: "CAND-1", to: id1 }));
  const c1 = await j1._recvType("cand");
  check("j1 got its addressed cand", c1 && c1.cand === "CAND-1");
  await t(200);
  check("j2 saw no cand", !j2._drain().some((f) => f && f.t === "cand"));

  // Upstream: answers/cands stamped with the sender's id.
  j1.send(JSON.stringify({ t: "answer", sdp: "ANS-1", pv: "25" }));
  const a1 = await host._recvType("answer");
  check("answer stamped from=j1 (pv preserved)",
        a1 && a1.sdp === "ANS-1" && a1.from === id1 && a1.pv === "25");
  j2.send(JSON.stringify({ t: "cand", mid: "0", cand: "UP-2" }));
  const uc = await host._recvType("cand");
  check("upstream cand stamped from=j2", uc && uc.cand === "UP-2" && uc.from === id2);

  // Legacy unaddressed offer: oldest connected joiner (j1) only.
  host.send(JSON.stringify({ t: "offer", sdp: "SDP-LEGACY", pv: "25" }));
  const lo = await j1._recvType("offer");
  check("unaddressed offer went to the oldest joiner", lo && lo.sdp === "SDP-LEGACY");
  await t(200);
  check("unaddressed offer skipped j2", !j2._drain().some((f) => f && f.t === "offer"));

  host.close(); j1.close(); j2.close();
  finish("RELAY-ISOLATION-TEST-OK");
})();
