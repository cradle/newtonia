// A rejoin while the host is in reclaim grace still gets TURN servers
// (join_should_mint). Before this, a host that dropped its socket (a
// console suspending, a phone backgrounded) left its auto-rejoining peer
// STUN-only, and a relay-only or hard-NAT client sat out a 40 s ICE
// timeout (Xbox dev kit, 2026-10-20).
//
// Run against its OWN `wrangler dev --local --port 8789 --var FAKE_TURN:1`
// (SIGNAL_TEST_URL=ws://127.0.0.1:8789/ws), which mints a placeholder
// server instead of calling Cloudflare. Not the shared :8787 server: the
// extra ice frames would reorder the older tests' raw frame reads.
import { connect, t, check, finish, host_room } from "./ws_harness.mjs";

// Every frame a fresh join receives in its first moments, then closes it.
async function join_frames(qs) {
  const ws = await connect(qs);
  await t(400);
  const frames = ws._drain().filter(Boolean);
  ws.close();
  await t(200);
  return frames;
}
const turn_count = (frames) => frames.filter((f) => f.t === "ice").length;
const joined = (frames) => frames.some((f) => f.t === "joined");

const { host, code } = await host_room();

let f = await join_frames(`?role=join&code=${code}`);
check("live host: join admitted", joined(f));
check("live host: join gets TURN", turn_count(f) > 0);
await host._drain();

host.close();
await t(400);

f = await join_frames(`?role=join&code=${code}&rejoin=1`);
check("host in grace: rejoin admitted", joined(f));
check("host in grace: rejoin gets TURN", turn_count(f) > 0);

f = await join_frames(`?role=join&code=${code}`);
check("host in grace: fresh join gets no TURN", turn_count(f) === 0);

finish("REJOIN TURN OK");
