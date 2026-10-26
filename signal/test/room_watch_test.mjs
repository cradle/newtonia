// A seated joiner keeping its socket for the game as a WATCHER ({t:"watch"},
// fork xbox/NETPLAY_XBOX.md 7c): an Xbox console checks everyone it plays
// with, including whoever takes a free seat mid-game, so it keeps hearing
// the identity frames of joiners that arrive after it. A watcher is no
// longer pairing: the host's unaddressed offers and candidates skip it, and
// it is not counted toward the room's capacity. Needs FAKE_VERIFY=1.
import { t, check, finish, host_room, join_room } from "./ws_harness.mjs";

const XBOX = 6;
const offers = (frames) => frames.filter((f) => f && f.t === "offer");
const cands = (frames) => frames.filter((f) => f && f.t === "cand");

(async () => {
  const { host, code } = await host_room();
  host.send(JSON.stringify({ t: "identity", platform: XBOX, name: "", cred: "XUID-H" }));
  await t(250);

  // Xbox joiner A is seated (its game started) and keeps its socket.
  const a = await join_room(code);
  await host._recvType("peer");
  a.send(JSON.stringify({ t: "identity", platform: XBOX, name: "", cred: "XUID-A" }));
  await host._recvType("identity");
  await t(250);
  a._drain();
  a.send(JSON.stringify({ t: "watch" }));
  const ack = await a._recvType("watching");
  check("the worker acknowledged the watch", !!ack);

  // The host's rejoin door offers unaddressed (a 2P-style re-offer): it must
  // not reach the watcher, and waits for the next arrival instead.
  host.send(JSON.stringify({ t: "offer", sdp: "v=0 REOFFER" }));
  host.send(JSON.stringify({ t: "cand", mid: "0", cand: "candidate:1 1 udp 1 10.0.0.1 9 typ host" }));
  await t(300);
  const a_frames = a._drain();
  check("the watcher got no unaddressed offer", offers(a_frames).length === 0);
  check("the watcher got no unaddressed candidate", cands(a_frames).length === 0);

  // Someone takes the free seat: the stored offer is theirs, and the watcher
  // hears who they are, with the Xbox user id.
  const c = await join_room(code);
  const c_id = (await host._recvType("peer")).from;
  const c_offer = await c._recvType("offer");
  check("the newcomer got the stored offer", c_offer && c_offer.sdp === "v=0 REOFFER");
  c.send(JSON.stringify({ t: "identity", platform: XBOX, name: "", cred: "XUID-C" }));
  await host._recvType("identity");
  await t(300);
  const a_c = a._drain().filter((f) => f && f.t === "identity" &&
                                      f.role === "joiner" && f.from === c_id);
  check("the watcher heard the newcomer",
        a_c.length > 0 && a_c[a_c.length - 1].verified === true &&
        a_c[a_c.length - 1].xuid === "XUID-C");

  // Capacity counts joiners still pairing: the watcher's seat is in the
  // host's own seat report, which this host never sent.
  check("the room still takes two more joiners beside the watcher",
        !!(await join_room(code)) && !!(await join_room(code)));

  host.close(); a.close(); c.close();
  finish("ROOM-WATCH-TEST-OK");
})();
