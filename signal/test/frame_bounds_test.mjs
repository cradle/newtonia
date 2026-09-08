// Protocol test for the signal worker's frame bounds (Workers review
// 2026-09-08, F4) against the REAL worker under wrangler dev — the unit
// test (room_unit_test.mjs) proves the same rules on a fake state; this
// proves them on workerd's sockets, close codes included:
//
//   - an offer/answer is forwarded REBUILT from allowlisted fields: an
//     extra field the sender put in the frame never reaches the peer
//   - a candidate's `mid` is bounded (over-long -> "0", short kept, a
//     number accepted)
//   - a frame over MAX_FRAME_LEN is dropped whole, and the sender's socket
//     stays open (it is not a flood on its own)
//   - a frame FLOOD (count) and a byte flood both close the flooding socket
//     with 1008, and the peer stays connected
//
// Run against `wrangler dev --local --port 8787 --var FAKE_VERIFY:1
//   --var RATE_HOST_LIMIT:200 --var RATE_JOIN_LIMIT:500`
// (deploy-signal.yml's "Protocol tests" step). Node 22+.
import { connect, host_room, join_room, check, finish, t } from "./ws_harness.mjs";

function watch_close(ws) {
  const state = { code: null };
  ws.addEventListener("close", (e) => { state.code = e.code; });
  return state;
}
const send = (ws, o) => ws.send(JSON.stringify(o));

// ---- allowlisted forwards ---------------------------------------------------
{
  const { host, code } = await host_room();
  const joiner = await join_room(code);
  await host._recvType("peer");  // join event

  send(host, { t: "offer", sdp: "v=0", pv: "29", to: "1",
               extra: "y".repeat(2000), nested: { deep: true } });
  const off = await joiner._recvType("offer");
  check("offer reaches the joiner", !!off);
  check("offer carries only t/sdp/pv",
        !!off && JSON.stringify(off) === JSON.stringify({ t: "offer", sdp: "v=0", pv: "29" }));

  send(joiner, { t: "answer", sdp: "v=0", pv: "29", extra: "z".repeat(2000) });
  const ans = await host._recvType("answer");
  check("answer reaches the host", !!ans);
  check("answer carries only t/sdp/pv/from",
        !!ans && JSON.stringify(ans) ===
            JSON.stringify({ t: "answer", sdp: "v=0", pv: "29", from: "1" }));

  send(host, { t: "cand", cand: "c1", mid: "m".repeat(5000) });
  send(host, { t: "cand", cand: "c2", mid: "1" });
  send(host, { t: "cand", cand: "c3", mid: 0 });
  const mids = [];
  for (let i = 0; i < 3; i++) { const c = await joiner._recvType("cand"); mids.push(c && c.mid); }
  check("candidate mids bounded (over-long -> \"0\", short kept, number ok)",
        JSON.stringify(mids) === JSON.stringify(["0", "1", "0"]));

  // Over MAX_FRAME_LEN (24 KB) but well under the byte budget: dropped
  // whole, nothing relayed, socket stays open.
  const hc = watch_close(host);
  send(host, { t: "offer", sdp: "v=0", pv: "29", to: "1", pad: "p".repeat(30 * 1024) });
  send(host, { t: "offer", sdp: "v=1", pv: "29", to: "1" });  // a marker after it
  const after = await joiner._recvType("offer");
  check("oversized frame is dropped whole (the next offer is what arrives)",
        !!after && after.sdp === "v=1");
  await t(200);
  check("oversized frame alone does not close the sender", hc.code === null);
  host.close(); joiner.close();
}

// ---- frame-count flood --------------------------------------------------------
{
  const { host, code } = await host_room();
  const joiner = await join_room(code);
  await host._recvType("peer");
  const hc = watch_close(host), jc = watch_close(joiner);
  for (let i = 0; i < 320; i++) send(host, { t: "cand", cand: "c", mid: "0" });
  await t(1500);
  check("301st frame in the window closes the flooding socket with 1008", hc.code === 1008);
  check("the joiner is not closed by the host's flood", jc.code === null);
  const got = joiner._drain().filter((f) => f && f.t === "cand").length;
  check("relayed candidates stop at the budget (300)", got === 300);
  joiner.close();
}

// ---- byte flood ---------------------------------------------------------------
{
  const { host, code } = await host_room();
  const joiner = await join_room(code);
  await host._recvType("peer");
  const hc = watch_close(host);
  // 70 x 16 KB > 1 MiB inside one window; each frame is under MAX_FRAME_LEN.
  for (let i = 0; i < 70; i++) send(host, { t: "offer", sdp: "s".repeat(16000), to: "1" });
  await t(1500);
  check("byte volume past 1 MiB in the window closes the socket with 1008", hc.code === 1008);
  joiner.close();
}

finish("FRAME-BOUNDS-OK");
