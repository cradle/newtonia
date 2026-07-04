// Newtonia netplay signaling — room codes + WebRTC offer/answer relay.
// See NETPLAY.md (Milestone 2). One Durable Object instance per room code;
// the worker only mints codes and routes WebSocket upgrades to the room.
//
// Wire protocol (JSON text frames, all fields lowercase):
//   host  -> /ws?role=host            {t:"room", code}        assigned code
//   join  -> /ws?role=join&code=ABCD  {t:"joined"}             or {t:"err",reason}
//   host  -> {t:"offer", sdp}         relayed to the joiner (also replayed to
//                                     a joiner who arrives after the offer)
//   join  -> {t:"answer", sdp}        relayed to the host
//   both  <- {t:"peer", ev:"join"|"leave"}
//   any   <- {t:"err", reason:"no-such-room"|"room-full"|"expired"}
//
// Rooms hold exactly one host and at most one joiner. The joiner slot
// reopens when the joiner disconnects (Milestone 2 rejoin); the room dies
// when the host disconnects or after ROOM_TTL_MS.

const CODE_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // no 0/O/1/I
const CODE_LEN = 4;
const ROOM_TTL_MS = 2 * 60 * 60 * 1000;

function random_code() {
  const bytes = crypto.getRandomValues(new Uint8Array(CODE_LEN));
  let code = "";
  for (const b of bytes) code += CODE_ALPHABET[b % CODE_ALPHABET.length];
  return code;
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname !== "/ws")
      return new Response("newtonia-signal", { status: 200 });
    if (request.headers.get("Upgrade") !== "websocket")
      return new Response("expected websocket", { status: 426 });

    const role = url.searchParams.get("role");
    if (role === "host") {
      // Mint an unused code: a fresh Room accepts a host only if it has
      // none; collisions (live room with that code) refuse and we retry.
      for (let attempt = 0; attempt < 8; attempt++) {
        const code = random_code();
        const room = env.ROOMS.get(env.ROOMS.idFromName(code));
        const resp = await room.fetch(
            new Request(`https://room/host?code=${code}`, request));
        if (resp.status !== 409) return resp;
      }
      return new Response("no free room codes", { status: 503 });
    }

    if (role === "join") {
      const code = (url.searchParams.get("code") || "").toUpperCase();
      if (code.length !== CODE_LEN ||
          [...code].some((c) => !CODE_ALPHABET.includes(c)))
        return new Response("bad code", { status: 400 });
      const room = env.ROOMS.get(env.ROOMS.idFromName(code));
      return room.fetch(new Request(`https://room/join?code=${code}`, request));
    }

    return new Response("bad role", { status: 400 });
  },
};

export class Room {
  constructor(state) {
    this.state = state;
    this.host = null;    // WebSocket
    this.joiner = null;  // WebSocket
    this.offer = null;   // last offer SDP, replayed to late joiners
    this.created = 0;
  }

  async fetch(request) {
    const url = new URL(request.url);
    const code = url.searchParams.get("code");
    const now = Date.now();

    if (this.host && now - this.created > ROOM_TTL_MS) this.expire();

    if (url.pathname === "/host") {
      if (this.host) return new Response("room in use", { status: 409 });
      const pair = new WebSocketPair();
      this.accept_host(pair[1], code, now);
      return new Response(null, { status: 101, webSocket: pair[0] });
    }

    if (url.pathname === "/join") {
      if (!this.host) return this.reject_ws("no-such-room");
      if (this.joiner) return this.reject_ws("room-full");
      const pair = new WebSocketPair();
      this.accept_joiner(pair[1]);
      return new Response(null, { status: 101, webSocket: pair[0] });
    }

    return new Response("not found", { status: 404 });
  }

  // Refusals still complete the WS upgrade so the client gets a readable
  // {t:"err"} frame instead of an opaque HTTP error it may not surface.
  reject_ws(reason) {
    const pair = new WebSocketPair();
    pair[1].accept();
    pair[1].send(JSON.stringify({ t: "err", reason }));
    pair[1].close(1000);
    return new Response(null, { status: 101, webSocket: pair[0] });
  }

  accept_host(ws, code, now) {
    ws.accept();
    this.host = ws;
    this.offer = null;
    this.created = now;
    ws.send(JSON.stringify({ t: "room", code }));
    ws.addEventListener("message", (m) => this.from_host(m));
    const drop = () => this.expire();
    ws.addEventListener("close", drop);
    ws.addEventListener("error", drop);
  }

  accept_joiner(ws) {
    ws.accept();
    this.joiner = ws;
    ws.send(JSON.stringify({ t: "joined" }));
    if (this.host) this.host.send(JSON.stringify({ t: "peer", ev: "join" }));
    if (this.offer) ws.send(JSON.stringify({ t: "offer", sdp: this.offer }));
    ws.addEventListener("message", (m) => this.from_joiner(m));
    const drop = () => this.drop_joiner();
    ws.addEventListener("close", drop);
    ws.addEventListener("error", drop);
  }

  from_host(m) {
    let msg;
    try { msg = JSON.parse(m.data); } catch (e) { return; }
    if (msg.t === "offer" && typeof msg.sdp === "string") {
      this.offer = msg.sdp; // kept for a joiner (or rejoiner) arriving later
      if (this.joiner) this.joiner.send(JSON.stringify(msg));
    }
  }

  from_joiner(m) {
    let msg;
    try { msg = JSON.parse(m.data); } catch (e) { return; }
    if (msg.t === "answer" && typeof msg.sdp === "string") {
      if (this.host) this.host.send(JSON.stringify(msg));
    }
  }

  drop_joiner() {
    this.joiner = null;
    if (this.host) this.host.send(JSON.stringify({ t: "peer", ev: "leave" }));
  }

  expire() {
    const bye = JSON.stringify({ t: "err", reason: "expired" });
    for (const ws of [this.host, this.joiner]) {
      if (!ws) continue;
      try { ws.send(bye); ws.close(1000); } catch (e) {}
    }
    this.host = null;
    this.joiner = null;
    this.offer = null;
  }
}
