// Relay-only privacy (signal/src/worker.js header): a socket opened with
// ro=1 makes its pairings private, and the worker then relays only
// `typ relay` candidates and SDPs stripped of every other address, both
// ways. Pure node, the same fake Durable Object state as
// room_unit_test.mjs.
//
// Run: node test/relay_privacy_test.mjs
import { Room, cand_is_relay, strip_direct_sdp, mask_related } from "../src/worker.js";

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

async function host(room, ro = false) {
  const ws = new FakeWs();
  await room.accept_host(ws, "ABCDE", Date.now(), [], ro);
  return ws;
}
async function joiner(room, ro = false) {
  const ws = new FakeWs();
  await room.accept_joiner(ws, [], ro);
  return ws;
}
const frame = (o) => JSON.stringify(o);


// Candidate lines in libdatachannel's shape. The addresses are documentation
// ranges; the test only looks at whether they survive.
const HOST = "a=candidate:1 1 UDP 2122317823 192.0.2.10 50000 typ host";
const SRFLX = "a=candidate:2 1 UDP 1686109951 198.51.100.7 50001 typ srflx raddr 0.0.0.0 rport 0";
const RELAY = "a=candidate:3 1 UDP 25108223 203.0.113.9 60000 typ relay raddr 198.51.100.7 rport 50001";
const SDP = ["v=0", "o=rtc 1 0 IN IP4 127.0.0.1", "s=-", "t=0 0",
             "m=application 50001 UDP/DTLS/SCTP webrtc-datachannel",
             "c=IN IP4 198.51.100.7", "a=mid:0", HOST, SRFLX, RELAY,
             "a=end-of-candidates", ""].join("\r\n");
const DIRECT = ["192.0.2.10", "198.51.100.7"];
const leaks = (s) => DIRECT.some((a) => s.includes(a));
const send_cands = async (ws, to) => {
  for (const c of [HOST, SRFLX, RELAY])
    await room_.webSocketMessage(ws, frame({ t: "cand", mid: "0", cand: c, ...(to ? { to } : {}) }));
};
let room_ = null;

// ---- the filters themselves -------------------------------------------------
check("filter: relay candidate passes", cand_is_relay(RELAY));
check("filter: host and srflx are dropped", !cand_is_relay(HOST) && !cand_is_relay(SRFLX));
check("filter: an unparseable candidate is dropped", !cand_is_relay("garbage"));
{
  const s = strip_direct_sdp(SDP);
  check("filter: SDP keeps only the relay candidate",
        s.includes("typ relay") && !s.includes("typ host") && !s.includes("typ srflx"), s);
  check("filter: SDP default address is blanked",
        s.includes("c=IN IP4 0.0.0.0") && /m=application 9 /.test(s) && !leaks(s), s);
  check("filter: SDP keeps its CRLF line endings", !/[^\r]\n/.test(s) && s.endsWith("\r\n"));
  check("filter: a relay candidate's related address is masked",
        mask_related(RELAY).endsWith("typ relay raddr 0.0.0.0 rport 0") && !leaks(mask_related(RELAY)));
}

// ---- a relay-only HOST: nothing direct reaches the joiner, and the
// ---- joiner's direct addresses never reach the host ------------------------
{
  const { room } = await make_room(); room_ = room;
  const h = await host(room, true);
  const j = await joiner(room);
  await room.webSocketMessage(h, frame({ t: "offer", sdp: SDP, to: "1" }));
  await send_cands(h, "1");
  await room.webSocketMessage(j, frame({ t: "answer", sdp: SDP }));
  await send_cands(j);
  const to_j = JSON.stringify(j.sent), to_h = JSON.stringify(h.sent);
  check("ro host: joiner gets the offer, stripped", j.of("offer").length === 1 && !leaks(j.of("offer")[0].sdp));
  check("ro host: joiner gets only the relay candidate",
        j.of("cand").length === 1 && j.of("cand")[0].cand === mask_related(RELAY), to_j);
  check("ro host: host gets the answer, stripped", h.of("answer").length === 1 && !leaks(h.of("answer")[0].sdp));
  check("ro host: host gets only the relay candidate",
        h.of("cand").length === 1 && h.of("cand")[0].cand === mask_related(RELAY), to_h);
  check("ro host: no direct address in either direction", !leaks(to_j) && !leaks(to_h));
}

// ---- a relay-only JOINER in a normal host's room ----------------------------
{
  const { room } = await make_room(); room_ = room;
  const h = await host(room);
  const j = await joiner(room, true);
  const k = await joiner(room);          // an ordinary second joiner
  await room.webSocketMessage(h, frame({ t: "offer", sdp: SDP, to: "1" }));
  await send_cands(h, "1");
  await room.webSocketMessage(h, frame({ t: "offer", sdp: SDP, to: "2" }));
  await send_cands(h, "2");
  await room.webSocketMessage(j, frame({ t: "answer", sdp: SDP }));
  await send_cands(j);
  check("ro joiner: its offer and candidates are stripped",
        !leaks(JSON.stringify(j.sent)) && j.of("cand").length === 1, JSON.stringify(j.sent));
  check("ro joiner: its answer and candidates reach the host stripped",
        h.of("answer").length === 1 && !leaks(JSON.stringify(h.sent)) &&
        h.of("cand").length === 1, JSON.stringify(h.sent));
  check("ro joiner: an ordinary joiner's pairing is untouched",
        k.of("cand").length === 3 && k.of("offer")[0].sdp === SDP);
  await room.webSocketMessage(k, frame({ t: "answer", sdp: SDP }));
  await send_cands(k);
  check("ro joiner: the ordinary joiner's answer reaches the host as sent",
        h.of("answer")[1].sdp === SDP && h.of("cand").length === 4);
}

// ---- legacy stored offer + buffered host candidates replay filtered --------
{
  const { room } = await make_room(); room_ = room;
  const h = await host(room, true);
  await room.webSocketMessage(h, frame({ t: "offer", sdp: SDP }));
  await send_cands(h);
  const j = await joiner(room);
  check("ro replay: stored offer is stripped on replay",
        j.of("offer").length === 1 && !leaks(j.of("offer")[0].sdp));
  check("ro replay: only the relay candidate is replayed",
        j.of("cand").length === 1 && j.of("cand")[0].cand === mask_related(RELAY), JSON.stringify(j.sent));
}

// ---- an ordinary room is untouched; the flag rides reclaim ------------------
{
  const { room } = await make_room(); room_ = room;
  const h = await host(room);
  const j = await joiner(room);
  await room.webSocketMessage(h, frame({ t: "offer", sdp: SDP, to: "1" }));
  await send_cands(h, "1");
  check("plain room: offer relayed byte for byte", j.of("offer")[0].sdp === SDP);
  check("plain room: every candidate relayed", j.of("cand").length === 3);
}
{
  const { room } = await make_room(); room_ = room;
  const h = await host(room, true);
  h.drop();
  await room.drop_host(h);
  const j = await joiner(room);
  const h2 = new FakeWs();
  await room.reclaim_host(h2, "ABCDE", [], true);
  await room.webSocketMessage(h2, frame({ t: "offer", sdp: SDP, to: "1" }));
  await send_cands(h2, "1");
  check("ro reclaim: the reclaimed host's frames are still stripped",
        !leaks(JSON.stringify(j.sent)) && j.of("cand").length === 1, JSON.stringify(j.sent));
}

// ---- the worker passes ro=1 from the client URL through to the room --------
{
  const { room } = await make_room(); room_ = room;
  const r1 = await room.fetch(new Request("https://room/host?code=ABCDE&ice=%5B%5D&ro=1"));
  const hs = last_pair[1];
  check("fetch: /host with ro=1 marks the room", r1.upgraded && room.r.host_ro === true && room.is_ro(hs));
  await room.fetch(new Request("https://room/join?code=ABCDE&ice=%5B%5D&ro=1"));
  check("fetch: /join with ro=1 tags the joiner", room.is_ro(last_pair[1]));
}

console.log(failures ? `${failures} FAILURE(S)` : "ALL PASS");
process.exit(failures ? 1 : 0);
