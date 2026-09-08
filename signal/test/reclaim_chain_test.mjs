// Protocol test for a host reclaim CHAIN against the real worker under
// wrangler dev (review of PR #527, round 2): a reclaimed host that drops
// again while the superseded socket may still be closing must start the
// grace window, so the rightful token can reclaim once more.
//
//   H1 hosts, J joins
//   H2 reclaims with the token  (the worker closes H1 itself)
//   H2 drops AT ONCE            (H1's close may still be completing)
//   -> J hears host-lost: grace started, the room is not expired
//   H3 reclaims with the token  -> accepted, J hears host-back
//   H3 offers                   -> J receives it: the relay is intact
//
// On the previous head the disconnect guard read the closing H1 as
// "someone else holds the room", skipped the grace stamp, and H3's reclaim
// was refused no-such-room. Whether H1 is still CLOSING when H2 drops is
// timing the test does not control; the assertion holds either way, which
// is the point.
//
// Run against `wrangler dev --local --port 8787 --var FAKE_VERIFY:1
//   --var RATE_HOST_LIMIT:200 --var RATE_JOIN_LIMIT:500`. Node 22+.
import { connect, join_room, check, finish, t } from "./ws_harness.mjs";

const send = (ws, o) => ws.send(JSON.stringify(o));
const reclaim = (code, token) =>
  connect(`?role=host&code=${code}&token=${encodeURIComponent(token)}`);

const h1 = await connect("?role=host");
const room = await h1._recvType("room");
check("hosted", !!room && !!room.code && !!room.token);
const { code, token } = room;
const j = await join_room(code);
await h1._recvType("peer");

const h2 = await reclaim(code, token);
const r2 = await h2._recvType("room");
check("H2 reclaims with the token", !!r2 && r2.token === token);
const back1 = await j._recvType("peer");
check("J hears host-back for H2", !!back1 && back1.ev === "host-back");

// Drop H2 immediately — before H1's worker-side close has surely completed.
h2.close();
const lost = await j._recvType("peer");
check("J hears host-lost (grace started, room not expired)",
      !!lost && lost.ev === "host-lost");

const h3 = await reclaim(code, token);
const r3 = await h3._recvType("room");
check("H3 reclaims with the same token after the chain", !!r3 && r3.token === token);
const back2 = await j._recvType("peer");
check("J hears host-back for H3", !!back2 && back2.ev === "host-back");

send(h3, { t: "offer", sdp: "v=0", pv: "29", to: "1" });
const off = await j._recvType("offer");
check("H3's offer is relayed to J", !!off && off.sdp === "v=0");

// And a deliberate close still kills the room for good.
send(h3, { t: "close" });
const bye = await j._recvType("err");
check("host close tells J host-closed", !!bye && bye.reason === "host-closed");
await t(200);
const late = await reclaim(code, token);
const lf = await late._recvType("err");
check("no reclaim after a deliberate close", !!lf && lf.reason === "no-such-room");

finish("RECLAIM-CHAIN-OK");
