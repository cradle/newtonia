// Joiner lookups (fork xbox/NETPLAY_XBOX.md 7c): a seated Xbox player asks
// who the room's joiners are on a short-lived {role:"look"} socket whenever
// its host names one it has never heard described. The answer comes from
// the worker's records, so a joiner whose relay socket has already closed
// is still described. A lookup socket is invisible to the room: the host
// hears nothing of it and it never takes the host's stored offer. Only a
// verified Xbox asker is told anything. Needs FAKE_VERIFY=1 (the fake
// "account" is the cred).
import { t, check, finish, host_room, join_room, connect } from "./ws_harness.mjs";

const XBOX = 6, STEAM = 2;

async function look(code, platform, cred, jids) {
  const ws = await connect("?role=look&code=" + code);
  const ready = await ws._recvType("lookready");
  ws.send(JSON.stringify({ t: "identity", platform, name: "", cred }));
  ws.send(JSON.stringify({ t: "lookup", jids: jids.join(",") }));
  const frames = [];
  for (let i = 0; i < 12; i++) {
    const f = await ws._recv();
    if (!f) break;
    frames.push(f);
    if (f.t === "looked") break;
  }
  ws.close();
  return { ready: !!ready, frames,
           looked: frames.find((f) => f.t === "looked") || null,
           ids: frames.filter((f) => f.t === "identity" && f.role === "joiner") };
}

(async () => {
  const { host, code } = await host_room();
  host.send(JSON.stringify({ t: "identity", platform: STEAM, name: "HOST", cred: "x" }));
  await t(250);

  // Xbox joiner A and PC joiner P are seated and close their relay sockets,
  // as every joiner does once its game link is up.
  const a = await join_room(code);
  const a_id = (await host._recvType("peer")).from;
  a.send(JSON.stringify({ t: "identity", platform: XBOX, name: "", cred: "XUID-A" }));
  await host._recvType("identity");
  const p = await join_room(code);
  const p_id = (await host._recvType("peer")).from;
  p.send(JSON.stringify({ t: "identity", platform: STEAM, name: "PC", cred: "x" }));
  await host._recvType("identity");
  await t(250);
  a.close(); p.close();
  await t(300);
  host._drain();

  // The host's rejoin door stores an unaddressed offer for the next arrival.
  host.send(JSON.stringify({ t: "offer", sdp: "v=0 REOFFER" }));
  await t(200);

  // An Xbox player asks about both: described from the records, A with its
  // Xbox user id, P without one.
  const r = await look(code, XBOX, "XUID-ME", [a_id, p_id, "999"]);
  check("the lookup socket was accepted", r.ready);
  const ra = r.ids.find((f) => f.from === a_id);
  const rp = r.ids.find((f) => f.from === p_id);
  check("a closed xbox joiner is described with its id",
        ra && ra.verified === true && ra.xuid === "XUID-A");
  check("a closed pc joiner is described without an id",
        rp && rp.platform === STEAM && !("xuid" in rp));
  check("the answer says it is complete",
        r.looked && r.looked.ok === true && r.looked.pending === "" &&
        r.looked.jids.split(",").sort().join() === [a_id, p_id, "999"].sort().join());
  check("the lookup got no offer", !r.frames.some((f) => f && f.t === "offer"));
  await t(200);
  check("the host heard nothing of the lookup",
        host._drain().filter((f) => f && (f.t === "peer" || f.t === "identity")).length === 0);

  // Whoever really joins next still gets the stored offer.
  const c = await join_room(code);
  const c_id = (await host._recvType("peer")).from;
  const c_offer = await c._recvType("offer");
  check("the next joiner got the stored offer", c_offer && c_offer.sdp === "v=0 REOFFER");

  // C is connected but has not said who it is: pending, to ask again.
  const r2 = await look(code, XBOX, "XUID-ME", [c_id]);
  check("a joiner not yet described is pending",
        r2.looked && r2.looked.ok === true && r2.looked.pending === c_id &&
        r2.ids.length === 0);
  // Its platform verify in flight: still pending.
  c.send(JSON.stringify({ t: "identity", platform: XBOX, name: "", cred: "SLOW:1500" }));
  await t(200);
  const r3 = await look(code, XBOX, "XUID-ME", [c_id]);
  check("a joiner mid-verify is pending", r3.looked && r3.looked.pending === c_id);
  await t(1800);
  const r4 = await look(code, XBOX, "XUID-ME", [c_id]);
  const r4c = r4.ids.find((f) => f.from === c_id);
  check("once verified it is described", r4.looked && r4.looked.pending === "" &&
        r4c && r4c.verified === true);

  // A joiner whose socket closes while its verify is still running (review
  // on #628): pending, not settled as nobody, until the attestation lands.
  const d = await join_room(code);
  const d_id = (await host._recvType("peer")).from;
  d.send(JSON.stringify({ t: "identity", platform: XBOX, name: "", cred: "SLOW:800" }));
  await t(20);
  d.close();
  await t(100);
  const r6 = await look(code, XBOX, "XUID-ME", [d_id]);
  check("a closed joiner mid-verify is pending",
        r6.looked && r6.looked.pending === d_id && r6.ids.length === 0);
  await t(1100);
  const r7 = await look(code, XBOX, "XUID-ME", [d_id]);
  const r7d = r7.ids.find((f) => f.from === d_id);
  check("once its verify lands it is described",
        r7.looked && r7.looked.pending === "" && r7d && r7d.verified === true);

  // A player who is not a verified Xbox player is told nothing.
  const r5 = await look(code, STEAM, "x", [a_id, p_id]);
  check("a non-xbox asker is told nothing",
        r5.looked && r5.looked.ok === false && r5.ids.length === 0);

  // Lookups don't take seats: the room still has its free one.
  check("the room still takes a joiner", !!(await join_room(code)));

  host.close(); c.close();
  finish("ROOM-LOOKUP-TEST-OK");
})();
