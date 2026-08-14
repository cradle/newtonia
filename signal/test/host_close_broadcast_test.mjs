// PB-D5 family 6 — host-close broadcast: a deliberate {t:"close"} tears
// the room down NOW for every joiner (err host-closed, no grace), and a
// subsequent join finds the tombstone.
import { connect, check, finish, host_room, join_room } from "./ws_harness.mjs";

(async () => {
  const { host, code } = await host_room();
  const joiners = [];
  for (let i = 0; i < 3; i++) {
    joiners.push(await join_room(code));
    await host._recvType("peer");
  }

  host.send(JSON.stringify({ t: "close" }));
  // Join IMMEDIATELY, racing the close on purpose: whichever wins, the
  // outcome is the same err — a join processed first is seated and then
  // swept by expire's broadcast, one processed after must hit the closed
  // tombstone BEFORE any socket-liveness check (the host's just-closed
  // socket can linger in getWebSockets(), and reading it as "alive"
  // seated this joiner in a dead room in silence — the CI-only flake of
  // 2026-08-14, which the leisurely late join below only caught when a
  // loaded runner delayed the close handshake).
  const early = await connect("?role=join&code=" + code);
  for (let i = 0; i < 3; i++) {
    const f = await joiners[i]._recvType("err");
    check(`joiner ${i} got host-closed`, f && f.reason === "host-closed");
  }
  const eerr = await early._recvType("err");
  check("immediate join raced the close and still heard host-closed",
        eerr && eerr.reason === "host-closed");

  const late = await connect("?role=join&code=" + code);
  const err = await late._recvType("err");
  check("late join finds the tombstone", err && err.reason === "host-closed");

  finish("HOST-CLOSE-BROADCAST-TEST-OK");
})();
