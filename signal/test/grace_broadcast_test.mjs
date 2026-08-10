// PB-D5 family 5 — grace with N joiners: an abrupt host drop sends
// host-lost to EVERY joiner; the token reclaim sends host-back to every
// joiner and replays every jid's attested identity to the fresh host
// socket.
import { connect, t, check, finish, host_room, join_room } from "./ws_harness.mjs";
import { BASE } from "./ws_harness.mjs";

(async () => {
  const { host, code } = await host_room();
  // host_room consumed the room frame — but we need the token; re-derive
  // by reading it off a fresh room instead.
  host.close();
  const host2 = await connect("?role=host");
  const room = await host2._recvType("room");
  const code2 = room.code, token = room.token;

  const joiners = [];
  const ids = [];
  for (let i = 0; i < 3; i++) {
    const j = await join_room(code2);
    joiners.push(j);
    ids.push((await host2._recvType("peer")).from);
    // Attest each joiner so the reclaim replay below has something to say.
    j.send(JSON.stringify({ t: "identity", platform: 2, name: "J" + i, cred: "x" }));
    await host2._recvType("identity");
  }

  // Abrupt drop (no close frame): every joiner hears host-lost.
  host2.close();
  for (let i = 0; i < 3; i++) {
    const f = await joiners[i]._recvType("peer");
    check(`joiner ${i} heard host-lost`, f && f.ev === "host-lost");
  }

  // Reclaim with the token: every joiner hears host-back; the fresh host
  // socket is replayed all three attested identities with their ids.
  const re = await connect(`?role=host&code=${code2}&token=${encodeURIComponent(token)}`);
  const reroom = await re._recvType("room");
  check("reclaim returned the room", reroom && reroom.code === code2);
  const seen = new Set();
  for (let i = 0; i < 3; i++) {
    const f = await re._recvType("identity");
    if (f && f.role === "joiner") seen.add(f.from);
  }
  check("reclaimed host replayed every jid's identity",
        ids.every((id) => seen.has(id)));
  for (let i = 0; i < 3; i++) {
    const f = await joiners[i]._recvType("peer");
    check(`joiner ${i} heard host-back`, f && f.ev === "host-back");
  }

  re.close();
  for (const j of joiners) j.close();
  finish("GRACE-BROADCAST-TEST-OK");
})();
