// Waiting-room host reconnect: a joiner who knocks while the host's socket
// is down gets no join event (there is no host to send it to). The token
// reclaim names every joiner still on the room with {t:"peer",
// ev:"waiting", from} so the host can offer them a seat — including the
// one who was there before the drop — and a joiner that left in the gap
// is not named.
import { connect, check, finish, join_room } from "./ws_harness.mjs";

(async () => {
  const host = await connect("?role=host");
  const room = await host._recvType("room");
  const code = room.code, token = room.token;

  // One joiner before the drop, one who comes and goes in the gap, one
  // who arrives in the gap and stays.
  const early = await join_room(code);
  const earlyId = (await host._recvType("peer")).from;

  host.close();  // abrupt drop: the room enters grace
  await early._recvType("peer");  // host-lost

  const gone = await join_room(code);
  const late = await join_room(code);
  gone.close();
  await new Promise((r) => setTimeout(r, 300));

  const re = await connect(`?role=host&code=${code}&token=${encodeURIComponent(token)}`);
  const reroom = await re._recvType("room");
  check("reclaim returned the room", reroom && reroom.code === code);
  const named = new Set();
  for (let i = 0; i < 2; i++) {
    const f = await re._recvType("peer");
    check(`frame ${i} is a waiting notice`, f && f.ev === "waiting");
    if (f) named.add(f.from);
  }
  check("the pre-drop joiner is named", named.has(earlyId));
  check("two joiners named", named.size === 2);
  // Nothing more: the joiner that left in the gap is not named.
  await new Promise((r) => setTimeout(r, 500));
  const extra = re._drain().filter((f) => f.t === "peer" && f.ev === "waiting");
  check("the departed joiner is not named", extra.length === 0);

  // The late joiner is reachable by an addressed offer now.
  const lateId = [...named].find((id) => id !== earlyId);
  re.send(JSON.stringify({ t: "offer", sdp: "v=0 test", to: lateId }));
  const off = await late._recvType("offer");
  check("the late joiner got its addressed offer", off && off.sdp === "v=0 test");

  re.close(); early.close(); late.close();
  finish("RECLAIM-WAITING-TEST-OK");
})();
