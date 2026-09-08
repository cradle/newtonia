// Room Durable Object unit test — pure node, no wrangler. Drives the real
// Room class against a fake DurableObjectState (storage, alarm, tagged
// hibernation sockets) for the properties the Workers review (2026-09-08)
// found, none of which the wrangler protocol suites can reach (they need a
// 24 h-old room, a controlled clock, or a verify paused across a re-host):
//
//   F3  A LIVE host past ROOM_TTL_MS is expired by the alarm — not re-armed
//       at a deadline already in the past (which fires again at once: a hot
//       loop for as long as the socket stays up). At exactly the host-grace
//       deadline the room is finished, not re-armed for the TTL.
//   F4  Frames are bounded as a whole before parsing; offers/answers are
//       forwarded REBUILT from allowlisted fields (an extra field never
//       reaches the peer); `mid` is bounded before it is buffered into the
//       room record; a frame flood closes the socket.
//   F9  An identity verify still in flight when the room expires and a NEW
//       host takes the same code must not attest the new room's joiner 1
//       (jids restart per room).
//   Review of PR #527: getWebSockets() can still return a CLOSING socket
//       after close(), so an expired room must stay unavailable (/exists,
//       /join, alive()) while its host's close is completing. The fake
//       socket therefore stays REGISTERED in CLOSING after close(), like
//       the platform's, and is only dropped by an explicit finish_close().
//
// Run: node test/room_unit_test.mjs
import { Room } from "../src/worker.js";

let failures = 0;
function check(name, cond, detail) {
  console.log((cond ? "PASS " : "FAIL ") + name +
              (cond || detail === undefined ? "" : `  (${detail})`));
  if (!cond) failures++;
}

const HOUR = 60 * 60 * 1000;
const real_now = Date.now;
let clock = real_now();
Date.now = () => clock;

class FakeWs {
  constructor() {
    this.sent = []; this.open = true; this.closed = null;
    this.readyState = 1;   // OPEN, like the platform's
    this.gone = false;     // dropped from getWebSockets() — the close completed
  }
  send(s) { if (!this.open) throw new Error("closed"); this.sent.push(JSON.parse(s)); }
  // close() is async on the platform: the socket reads CLOSING (2) and
  // getWebSockets() may still return it until the close completes.
  close(code, reason) {
    this.open = false; this.readyState = 2; this.closed = { code, reason };
  }
  finish_close() { this.readyState = 3; this.gone = true; }
  // A socket that DROPPED (the peer went away): closed and already gone
  // from the listing, the case drop_host/drop_joiner see.
  drop() { this.open = false; this.readyState = 3; this.gone = true; }
  accept() {}   // reject_ws accepts the ephemeral server end before sending
  of(t) { return this.sent.filter((f) => f.t === t); }
}

// Room.fetch answers WebSocket upgrades with a 101 Response carrying the
// client end of a WebSocketPair — neither exists in Node. Stand-ins: a
// Response that records a 101 as such (Node refuses the status itself),
// and a pair of FakeWs whose SERVER end (the one the room sends on) is
// kept on `last_pair` for the test to read.
let last_pair = null;
globalThis.WebSocketPair = class {
  constructor() { this[0] = new FakeWs(); this[1] = new FakeWs(); last_pair = this; }
};
const RealResponse = globalThis.Response;
globalThis.Response = class extends RealResponse {
  constructor(body, init) {
    if (init && init.status === 101) {
      super(body, { ...init, status: 200 });
      this.upgraded = true;
    } else super(body, init);
  }
};

class FakeState {
  constructor() {
    this.store = new Map();
    this.sockets = [];
    this.alarm = null;
    this.alarms = [];
    this.storage = {
      get: async (k) => this.store.get(k),
      put: async (k, v) => { this.store.set(k, JSON.parse(JSON.stringify(v))); },
      setAlarm: async (t) => { this.alarm = t; this.alarms.push(t); },
      deleteAll: async () => { this.store.clear(); this.alarm = null; },
    };
  }
  blockConcurrencyWhile(fn) { return fn(); }
  acceptWebSocket(ws, tags) { ws.tags = tags; this.sockets.push(ws); }
  // Like the platform's: a CLOSING socket is still listed until its close
  // completes (finish_close).
  getWebSockets(tag) {
    return this.sockets.filter((w) => !w.gone && (!tag || w.tags.includes(tag)));
  }
  getTags(ws) { return ws.tags || []; }
}

async function make_room(env = {}) {
  const state = new FakeState();
  const room = new Room(state, env);
  await new Promise((r) => setImmediate(r));  // the ctor's storage load
  return { room, state };
}

async function host(room, created = Date.now()) {
  const ws = new FakeWs();
  await room.accept_host(ws, "ABCDE", created, []);
  return ws;
}
async function joiner(room) {
  const ws = new FakeWs();
  await room.accept_joiner(ws, []);
  return ws;
}
const frame = (o) => JSON.stringify(o);

// ---- F3: alarm deadline order ---------------------------------------------
{
  const { room, state } = await make_room();
  const h = await host(room, Date.now() - 25 * HOUR);  // created 25 h ago
  const armed_before = state.alarms.length;  // accept_host's own arm
  await room.alarm();
  check("F3: live host past TTL is expired by the alarm",
        !h.open && h.closed && h.closed.code === 1000 &&
        h.of("err").some((f) => f.reason === "expired"),
        JSON.stringify({ open: h.open, sent: h.sent }));
  check("F3: expired room's storage is released", state.store.size === 0);
  check("F3: no alarm re-armed in the past",
        state.alarms.slice(armed_before).every((t) => t >= Date.now()),
        JSON.stringify(state.alarms.slice(armed_before)));
  await room.alarm();
  check("F3: a second alarm is a no-op (no re-arm at all)",
        state.alarm === null, `${state.alarm}`);
}
{
  const { room, state } = await make_room();
  const created = Date.now() - 1 * HOUR;
  await host(room, created);
  await room.alarm();
  check("F3: live host inside TTL re-arms at the TTL, in the future",
        state.alarm === created + 24 * HOUR && state.alarm > Date.now(),
        `${state.alarm - Date.now()}`);
}
{
  // Host drops; at EXACTLY the grace deadline the room is finished.
  const { room, state } = await make_room();
  const h = await host(room);
  h.drop();
  await room.drop_host(h);
  const lost = room.r.host_lost_at;
  check("F3: drop armed the grace deadline", state.alarm === lost + 2 * 60 * 1000);
  clock = lost + 2 * 60 * 1000;
  await room.alarm();
  check("F3: at the grace deadline the room is cleaned up, not re-armed",
        state.store.size === 0 && state.alarm === null,
        JSON.stringify({ size: state.store.size, alarm: state.alarm }));
}
{
  // In grace with time left: re-armed at the grace end, not the TTL.
  const { room, state } = await make_room();
  const h = await host(room);
  h.drop();
  await room.drop_host(h);
  const lost = room.r.host_lost_at;
  clock = lost + 30 * 1000;
  await room.alarm();
  check("F3: mid-grace alarm re-arms at the grace end",
        state.alarm === lost + 2 * 60 * 1000, `${state.alarm - clock}`);
}

// ---- Review of PR #527: an expired room stays closed while its sockets
// ---- are still CLOSING ------------------------------------------------------
{
  const { room, state } = await make_room();
  const h = await host(room, Date.now() - 25 * HOUR);
  await room.alarm();                  // TTL cleanup: h.close() called...
  check("closing host: socket is CLOSING and still listed",
        h.readyState === 2 && state.getWebSockets("host").length === 1);
  check("closing host: the room is not alive", room.alive(Date.now()) === false);
  const ex = await (await room.fetch(new Request("https://room/exists"))).json();
  check("closing host: /exists reports no host", ex.host === false, JSON.stringify(ex));
  const jr = await room.fetch(new Request("https://room/join?code=ABCDE&ice=%5B%5D"));
  const server_end = last_pair && last_pair[1];
  check("closing host: /join is refused with no-such-room",
        jr.upgraded && server_end && !server_end.open &&
        server_end.of("err").some((f) => f.reason === "no-such-room") &&
        server_end.of("joined").length === 0,
        JSON.stringify(server_end && server_end.sent));
  // A NEW host may take the code at once — the closing socket does not
  // squat it — and it, not the closing one, is the host from then on.
  const hr = await room.fetch(new Request("https://room/host?code=ABCDE&ice=%5B%5D"));
  check("closing host: a fresh host is accepted on the code", hr.upgraded && hr.status !== 409,
        `${hr.status}`);
  check("closing host: the new socket is the host, not the closing one",
        room.hostWs() === last_pair[1] && room.hostWs() !== h);
  h.finish_close();
  check("closing host: once the close completes nothing changes",
        room.hostWs() === last_pair[1] && room.alive(Date.now()) === true);
}
{
  // The same window on the plain expire() path (host `close`, grace end).
  const { room } = await make_room();
  const h = await host(room);
  const j = await joiner(room);
  await room.expire();
  check("expire: closing sockets keep the room dead",
        h.readyState === 2 && j.readyState === 2 && room.alive(Date.now()) === false);
  const ex = await (await room.fetch(new Request("https://room/exists"))).json();
  check("expire: /exists reports no host and no joiner",
        ex.host === false && ex.joiner === false, JSON.stringify(ex));
}

{
  // Round 2: a RECLAIMED host drops while the superseded socket is still
  // CLOSING. The disconnect guard must not read the closing old socket as
  // "someone else holds the room" — that skipped the grace stamp, and the
  // next reclaim found no grace and expired the room on the rightful token.
  const { room, state } = await make_room();
  const h1 = await host(room);
  const token = room.r.host_token;
  const h2 = new FakeWs();
  await room.reclaim_host(h2, "ABCDE", []);
  check("reclaim: old socket is CLOSING and still listed, new one is the host",
        h1.readyState === 2 && state.getWebSockets("host").length === 2 &&
        room.hostWs() === h2);
  h2.drop();
  await room.drop_host(h2);
  check("reclaimed host drops while old is closing: grace starts",
        room.r.host_lost_at > 0 && state.alarm === room.r.host_lost_at + 2 * 60 * 1000,
        JSON.stringify({ lost: room.r.host_lost_at, alarm: state.alarm }));
  const rc = await (await room.fetch(new Request(
      `https://room/reclaim-check?token=${token}`))).json();
  check("...and the rightful token can still reclaim", rc.ok === true, JSON.stringify(rc));
  check("...and the room is alive (in grace)", room.alive(Date.now()) === true);
  // The old socket's own close finally completing changes nothing.
  h1.finish_close();
  await room.drop_host(h1);
  check("old socket's late close leaves the grace window as it was",
        room.r.host_lost_at > 0 && room.alive(Date.now()) === true);
}

// ---- F4: frame bounds ---------------------------------------------------
{
  // Oversized whole frame: dropped before parse, nothing relayed — and a
  // 3 MiB frame is a flood on its own, so the byte budget closes the socket.
  const { room } = await make_room();
  const h = await host(room);
  const j = await joiner(room);
  await room.webSocketMessage(h, frame({ t: "offer", sdp: "v=0",
                                         junk: "x".repeat(3 << 20) }));
  check("F4: oversized frame relays nothing", j.of("offer").length === 0);
  check("F4: a multi-megabyte frame trips the byte budget",
        !h.open && h.closed.code === 1008, JSON.stringify(h.closed));
}
{
  const { room } = await make_room();
  const h = await host(room);
  const j = await joiner(room);
  // Under the frame cap but with an extra field: forwarded WITHOUT it.
  await room.webSocketMessage(h, frame({ t: "offer", sdp: "v=0", pv: "29",
                                         extra: "y".repeat(2000), to: "1" }));
  const off = j.of("offer");
  check("F4: addressed offer forwarded from allowlisted fields only",
        off.length === 1 && JSON.stringify(off[0]) ===
            JSON.stringify({ t: "offer", sdp: "v=0", pv: "29" }),
        JSON.stringify(off));
  await room.webSocketMessage(j, frame({ t: "answer", sdp: "v=0", pv: "29",
                                         extra: "z".repeat(2000) }));
  const ans = h.of("answer");
  check("F4: answer forwarded from allowlisted fields, stamped from",
        ans.length === 1 && JSON.stringify(ans[0]) ===
            JSON.stringify({ t: "answer", sdp: "v=0", pv: "29", from: "1" }),
        JSON.stringify(ans));
  // Candidate mid bounded on the live relay too.
  await room.webSocketMessage(h, frame({ t: "cand", cand: "c", mid: "m".repeat(5000) }));
  await room.webSocketMessage(h, frame({ t: "cand", cand: "c", mid: "1" }));
  await room.webSocketMessage(h, frame({ t: "cand", cand: "c", mid: 0 }));
  const cands = j.of("cand").map((c) => c.mid);
  check("F4: relayed mids bounded (over-long -> \"0\", short kept, number ok)",
        JSON.stringify(cands) === JSON.stringify(["0", "1", "0"]),
        JSON.stringify(cands));
}
{
  // No joiner yet: candidates are BUFFERED into the room record — the mid
  // must be bounded before it is persisted.
  const { room, state } = await make_room();
  const h = await host(room);
  await room.webSocketMessage(h, frame({ t: "cand", cand: "c", mid: "m".repeat(5000) }));
  const stored = state.store.get("room");
  check("F4: buffered candidate's mid is bounded",
        stored.host_cands.length === 1 && stored.host_cands[0].mid === "0",
        JSON.stringify(stored.host_cands));
  check("F4: room record stays small",
        JSON.stringify(stored).length < 4096, `${JSON.stringify(stored).length}`);
}
{
  const { room } = await make_room();
  const h = await host(room);
  for (let i = 0; i < 300; i++)
    await room.webSocketMessage(h, frame({ t: "cand", cand: "c", mid: "0" }));
  check("F4: 300 frames in a window are still fine", h.open);
  await room.webSocketMessage(h, frame({ t: "cand", cand: "c", mid: "0" }));
  check("F4: the 301st frame closes the flooding socket",
        !h.open && h.closed.code === 1008, JSON.stringify(h.closed));
}
{
  const { room } = await make_room();
  const h = await host(room);
  for (let i = 0; i < 70; i++)
    await room.webSocketMessage(h, frame({ t: "offer", sdp: "s".repeat(16000) }));
  check("F4: byte volume also trips the budget (70 x 16 KB > 1 MiB)",
        !h.open && h.closed.code === 1008, JSON.stringify(h.closed));
}

// ---- F9: verify across a room generation ----------------------------------
{
  // Control: the slow verify lands on the joiner in the SAME room.
  const { room } = await make_room({ FAKE_VERIFY: "1" });
  const h = await host(room);
  const j = await joiner(room);
  const p = room.webSocketMessage(j, frame({ t: "identity", platform: 2,
                                             name: "OLDPILOT", cred: "SLOW:20" }));
  await new Promise((r) => setTimeout(r, 60));
  await p;
  const id = h.of("identity").find((f) => f.role === "joiner");
  check("F9 control: slow verify attests the announcing joiner",
        !!id && id.verified === true && id.name === "OLDPILOT" && id.from === "1",
        JSON.stringify(h.sent));
}
{
  const { room } = await make_room({ FAKE_VERIFY: "1" });
  const h = await host(room);
  const j = await joiner(room);                  // jid 1
  const p = room.webSocketMessage(j, frame({ t: "identity", platform: 2,
                                             name: "OLDPILOT", cred: "SLOW:30" }));
  await new Promise((r) => setImmediate(r));     // let it reach the await
  await room.expire();                           // room lifetime ends...
  const h2 = await host(room);                   // ...a new host takes the code
  const j2 = await joiner(room);                 // ...and its joiner is jid 1 again
  await new Promise((r) => setTimeout(r, 80));
  await p;
  check("F9: late verify does not attest the replacement room's joiner",
        !room.r.jids[1] || !room.r.jids[1].identity,
        JSON.stringify(room.r.jids));
  check("F9: the new host hears no identity for its joiner",
        h2.of("identity").length === 0, JSON.stringify(h2.sent));
  check("F9: old sockets were closed by the expiry", !h.open && !j.open && j2.open);
}

Date.now = real_now;
console.log(failures ? `${failures} FAILURE(S)` : "ALL PASS");
process.exit(failures ? 1 : 0);
