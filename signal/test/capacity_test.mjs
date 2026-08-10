// PB-D5 family 1 — capacity + slot reopen: a room admits MAX_JOINERS (3)
// joiners, refuses the 4th with room-full, and a slot reopens when its
// joiner disconnects. The host hears {t:"peer",ev:"join"/"leave"} with a
// per-joiner `from` id, and ids are never reused.
import { connect, t, check, finish, host_room, join_room } from "./ws_harness.mjs";

(async () => {
  const { host, code } = await host_room();

  const j1 = await join_room(code);
  const p1 = await host._recvType("peer");
  check("joiner 1 admitted, join event has from", p1 && p1.ev === "join" && !!p1.from);
  const j2 = await join_room(code);
  const p2 = await host._recvType("peer");
  const j3 = await join_room(code);
  const p3 = await host._recvType("peer");
  check("joiners 2+3 admitted", p2 && p2.ev === "join" && p3 && p3.ev === "join");
  check("join ids are distinct",
        new Set([p1.from, p2.from, p3.from]).size === 3);

  // 4th join: refused.
  const j4 = await connect("?role=join&code=" + code);
  const err = await j4._recvType("err");
  check("4th joiner refused room-full", err && err.reason === "room-full");

  // Drop joiner 2: host hears leave with ITS id; the slot reopens.
  j2.close();
  const leave = await host._recvType("peer");
  check("leave names the departed id", leave && leave.ev === "leave" &&
        leave.from === p2.from);
  const j5 = await join_room(code);
  const p5 = await host._recvType("peer");
  check("slot reopened for a new joiner", p5 && p5.ev === "join");
  check("reopened slot minted a FRESH id (never reused)",
        p5.from !== p1.from && p5.from !== p2.from && p5.from !== p3.from);

  host.close(); j1.close(); j3.close(); j5.close();
  finish("CAPACITY-TEST-OK");
})();
