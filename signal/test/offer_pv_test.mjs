// PB-D5 family 7 — per-offer pv: each ADDRESSED offer carries its own pv
// stamp end-to-end (two different stamps to two joiners in one room), an
// addressed offer with no pv arrives with none, and the legacy stored
// replay still carries pv (the pv_replay_test case, re-pinned here beside
// its per-jid siblings).
import { t, check, finish, host_room, join_room } from "./ws_harness.mjs";

(async () => {
  const { host, code } = await host_room();
  const j1 = await join_room(code);
  const id1 = (await host._recvType("peer")).from;
  const j2 = await join_room(code);
  const id2 = (await host._recvType("peer")).from;

  host.send(JSON.stringify({ t: "offer", sdp: "S1", pv: "25", to: id1 }));
  host.send(JSON.stringify({ t: "offer", sdp: "S2", pv: "26", to: id2 }));
  const o1 = await j1._recvType("offer");
  const o2 = await j2._recvType("offer");
  check("per-offer pv rides its own offer",
        o1 && o1.pv === "25" && o2 && o2.pv === "26");

  host.send(JSON.stringify({ t: "offer", sdp: "S3", to: id1 }));
  const o3 = await j1._recvType("offer");
  check("pv-less addressed offer arrives without pv",
        o3 && o3.sdp === "S3" && o3.pv === undefined);

  // Legacy stored replay keeps pv: park an unaddressed offer with no
  // joiner able to take it... both are connected, so drop them first.
  j1.close(); j2.close();
  await host._recvType("peer");
  await host._recvType("peer");
  host.send(JSON.stringify({ t: "offer", sdp: "S4", pv: "25" }));
  await t(200);
  const j3 = await join_room(code);
  const o4 = await j3._recvType("offer");
  check("legacy stored replay keeps pv", o4 && o4.sdp === "S4" && o4.pv === "25");

  host.close(); j3.close();
  finish("OFFER-PV-TEST-OK");
})();
