// M3-1 worker protocol test: host token + grace + reclaim.
// Run: node reclaim_test.js   (wrangler dev on :8787)
const BASE = "ws://127.0.0.1:8787/ws";

function connect(qs) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(BASE + qs);
    const inbox = [];
    const waiters = [];
    ws._recv = () => new Promise((res) => {
      if (inbox.length) return res(inbox.shift());
      waiters.push(res);
    });
    ws.onmessage = (m) => {
      const f = JSON.parse(m.data);
      if (waiters.length) waiters.shift()(f);
      else inbox.push(f);
    };
    ws.onopen = () => resolve(ws);
    ws.onerror = (e) => reject(new Error("ws error"));
  });
}

const t = (ms) => new Promise((r) => setTimeout(r, ms));
let failures = 0;
function check(name, cond) {
  console.log((cond ? "PASS " : "FAIL ") + name);
  if (!cond) failures++;
}

(async () => {
  // 1. Host a room, get code + token.
  const host1 = await connect("?role=host");
  const room = await host1._recv();
  check("room frame has code+token", room.t === "room" && room.code && room.token);
  const { code, token } = room;

  // 2. Joiner connects, host sees peer join.
  const join1 = await connect(`?role=join&code=${code}`);
  const joined = await join1._recv();
  check("joined", joined.t === "joined");
  const pj = await host1._recv();
  check("host sees peer join", pj.t === "peer" && pj.ev === "join");

  // 3. Host socket dies -> joiner told host-lost, room survives (grace).
  host1.close();
  const hl = await join1._recv();
  check("joiner sees host-lost", hl.t === "peer" && hl.ev === "host-lost");
  await t(300);

  // 4. A fresh host must NOT be able to squat the in-grace code.
  //    (direct probe: role=host mints random codes, so hit /exists via a
  //    join attempt on a second slot instead — full room refuses.)
  const squat = await connect(`?role=join&code=${code}`);
  const sq = await squat._recv();
  check("second joiner refused (room-full, room still alive)", sq.t === "err" && sq.reason === "room-full");

  // 5. Reclaim with a WRONG token is refused.
  const bad = await connect(`?role=host&code=${code}&token=nope`);
  const badf = await bad._recv();
  check("wrong token refused", badf.t === "err");

  // 6. Reclaim with the REAL token restores the host slot.
  const host2 = await connect(`?role=host&code=${code}&token=${encodeURIComponent(token)}`);
  const re = await host2._recv();
  check("reclaim gets room frame, same code", re.t === "room" && re.code === code);
  check("token stable across reclaim", re.token === token);
  const hb = await join1._recv();
  check("joiner sees host-back", hb.t === "peer" && hb.ev === "host-back");

  // 7. Fresh offer relays to the waiting joiner.
  host2.send(JSON.stringify({ t: "offer", sdp: "sdp-after-reclaim" }));
  const off = await join1._recv();
  check("offer relayed after reclaim", off.t === "offer" && off.sdp === "sdp-after-reclaim");
  join1.send(JSON.stringify({ t: "answer", sdp: "ans" }));
  const ans = await host2._recv();
  check("answer relayed after reclaim", ans.t === "answer" && ans.sdp === "ans");

  // 8. Joiner drop during grace: joiner leaves while host reconnects.
  //    (also verifies join-during-grace: drop host, drop joiner, rejoin.)
  host2.close();
  await join1._recv(); // host-lost
  join1.close();
  await t(300);
  const join2 = await connect(`?role=join&code=${code}`);
  const j2 = await join2._recv();
  check("join during grace accepted", j2.t === "joined");
  const host3 = await connect(`?role=host&code=${code}&token=${encodeURIComponent(token)}`);
  const re3 = await host3._recv();
  check("reclaim with joiner waiting", re3.t === "room");
  // joiner got host-back too
  const hb2 = await join2._recv();
  check("waiting joiner sees host-back", hb2.t === "peer" && hb2.ev === "host-back");

  // 9. Trickle ICE (M3-2b): candidates relay both ways, and the host's
  //    are buffered for a joiner arriving after the offer.
  host3.send(JSON.stringify({ t: "offer", sdp: "trickle-offer" }));
  await join2._recv(); // the offer relayed live
  host3.send(JSON.stringify({ t: "cand", mid: "0", cand: "candidate:h1" }));
  const c1 = await join2._recv();
  check("host cand relayed live", c1.t === "cand" && c1.cand === "candidate:h1");
  join2.send(JSON.stringify({ t: "cand", mid: "0", cand: "candidate:j1" }));
  const c2 = await host3._recv();
  check("joiner cand relayed", c2.t === "cand" && c2.cand === "candidate:j1");

  join2.close();
  await host3._recv(); // peer leave
  await t(300);
  // Candidates sent with no joiner present are buffered with the offer...
  host3.send(JSON.stringify({ t: "offer", sdp: "trickle-offer-2" }));
  host3.send(JSON.stringify({ t: "cand", mid: "0", cand: "candidate:h2" }));
  host3.send(JSON.stringify({ t: "cand", mid: "0", cand: "candidate:h3" }));
  await t(300);
  // ...and replayed to a late joiner right after the offer, in order.
  const join3 = await connect(`?role=join&code=${code}`);
  const j3 = await join3._recv();
  check("late joiner accepted", j3.t === "joined");
  const off3 = await join3._recv();
  check("offer replayed", off3.t === "offer" && off3.sdp === "trickle-offer-2");
  const c3 = await join3._recv();
  const c4 = await join3._recv();
  check("buffered cands replayed in order",
        c3.t === "cand" && c3.cand === "candidate:h2" &&
        c4.t === "cand" && c4.cand === "candidate:h3");

  // 10. Deliberate host close: the room dies NOW — the waiting joiner is
  //     told, no grace window, and the code is immediately unjoinable.
  host3.send(JSON.stringify({ t: "close" }));
  const byebye = await join3._recv();
  check("joiner told on host close", byebye.t === "err" && byebye.reason === "expired");
  await t(300);
  const join4 = await connect(`?role=join&code=${code}`);
  const j4 = await join4._recv();
  check("closed room rejects join (no grace)",
        j4.t === "err" && j4.reason === "no-such-room");
  join4.close?.();

  host3.close(); join3.close(); squat.close?.();
  console.log(failures ? `\n${failures} FAILURES` : "\nRECLAIM-TEST-OK");
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.error("test crashed:", e); process.exit(1); });
