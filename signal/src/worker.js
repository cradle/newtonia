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

const CODE_ALPHABET = "ABCDEGHJKLMNPQRSTUVWXYZ23456789"; // no 0/O/1/I (confusable) or F (game fullscreen key)
const CODE_LEN = 4;
const ROOM_TTL_MS = 2 * 60 * 60 * 1000;

// Per-IP rate limits (fixed window). Host-creates farm rooms + TURN
// credentials; join-attempts can brute-force the 4-letter code space.
const RATE_WINDOW_MS = 10 * 60 * 1000;
const HOST_LIMIT = 10;
const JOIN_LIMIT = 30;

// SDP frames are relayed verbatim into DO memory; cap them so a peer can't
// pin unbounded memory or flood the relay. Oversized frames are dropped.
const MAX_SDP_LEN = 16384;

function random_code() {
  const bytes = crypto.getRandomValues(new Uint8Array(CODE_LEN));
  let code = "";
  for (const b of bytes) code += CODE_ALPHABET[b % CODE_ALPHABET.length];
  return code;
}

// Short-lived Cloudflare Calls TURN credentials, minted per connection.
// Configured via secrets (wrangler secret put TURN_KEY_ID / TURN_API_TOKEN);
// without them this returns [] and the game stays STUN-only.
async function turn_ice_servers(env) {
  if (!env.TURN_KEY_ID || !env.TURN_API_TOKEN) return [];
  try {
    const resp = await fetch(
        `https://rtc.live.cloudflare.com/v1/turn/keys/${env.TURN_KEY_ID}/credentials/generate`,
        {
          method: "POST",
          headers: {
            Authorization: `Bearer ${env.TURN_API_TOKEN}`,
            "Content-Type": "application/json",
          },
          body: JSON.stringify({ ttl: 2 * 60 * 60 }),
        });
    if (!resp.ok) return [];
    const data = await resp.json();
    const s = data.iceServers;
    if (!s || !s.urls) return [];
    const urls = Array.isArray(s.urls) ? s.urls : [s.urls];
    return urls.map((u) => ({
      urls: u, username: s.username, credential: s.credential,
    }));
  } catch (e) {
    return [];
  }
}

// One cheap Limiter-DO round-trip per /ws request: it both counts and
// verdicts the attempt. Returns true when the attempt is within budget.
async function within_limit(env, ip, action) {
  const limiter = env.LIMITS.get(env.LIMITS.idFromName(ip));
  const resp = await limiter.fetch(`https://limiter/hit?action=${action}`);
  try {
    const data = await resp.json();
    return !!data.allowed;
  } catch (e) {
    return true; // never let a limiter hiccup lock everyone out
  }
}

// Rate-limit / policy refusal at the worker level. The upgrade is already
// confirmed a websocket by the time we reach these, so we complete it and
// hand back a readable {t:"err"} frame (same pattern as Room.reject_ws);
// a plain 429 is the fallback if somehow there is no upgrade.
function reject_ws(request, reason) {
  if (request.headers.get("Upgrade") !== "websocket")
    return new Response(reason, { status: 429 });
  const pair = new WebSocketPair();
  pair[1].accept();
  pair[1].send(JSON.stringify({ t: "err", reason }));
  pair[1].close(1000);
  return new Response(null, { status: 101, webSocket: pair[0] });
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname !== "/ws")
      return new Response("newtonia-signal", { status: 200 });
    if (request.headers.get("Upgrade") !== "websocket")
      return new Response("expected websocket", { status: 426 });

    // CF-Connecting-IP may be absent under miniflare (wrangler dev --local);
    // a fixed fallback key still lets the limiter exercise end to end.
    const ip = request.headers.get("CF-Connecting-IP") || "local";
    const role = url.searchParams.get("role");

    if (role === "host") {
      if (!(await within_limit(env, ip, "host")))
        return reject_ws(request, "rate-limited");
      // TURN credentials are minted only now — passing the limiter is the
      // authorization for this host attempt — immediately before we claim a
      // code. Missing TURN secrets still yield [] (STUN-only), unchanged.
      const ice = await turn_ice_servers(env);
      // Mint an unused code: a fresh Room accepts a host only if it has
      // none; collisions (live room with that code) refuse and we retry.
      for (let attempt = 0; attempt < 8; attempt++) {
        const code = random_code();
        const room = env.ROOMS.get(env.ROOMS.idFromName(code));
        const resp = await room.fetch(
            new Request(`https://room/host?code=${code}&ice=${encodeURIComponent(JSON.stringify(ice))}`, request));
        if (resp.status !== 409) return resp;
      }
      return new Response("no free room codes", { status: 503 });
    }

    if (role === "join") {
      const code = (url.searchParams.get("code") || "").toUpperCase();
      if (code.length !== CODE_LEN ||
          [...code].some((c) => !CODE_ALPHABET.includes(c)))
        return new Response("bad code", { status: 400 });
      if (!(await within_limit(env, ip, "join")))
        return reject_ws(request, "rate-limited");
      const room = env.ROOMS.get(env.ROOMS.idFromName(code));
      // Lightweight pre-check so credentials are minted only when there is
      // actually an open room to join (host present, joiner slot free). A
      // miss falls through to the real /join, which emits the proper error.
      let ice = [];
      try {
        const pre = await room.fetch(new Request("https://room/exists"));
        const st = await pre.json();
        if (st.host && !st.joiner) ice = await turn_ice_servers(env);
      } catch (e) {}
      return room.fetch(new Request(
          `https://room/join?code=${code}&ice=${encodeURIComponent(JSON.stringify(ice))}`, request));
    }

    return new Response("bad role", { status: 400 });
  },
};

// Per-IP fixed-window rate limiter. One instance per client IP (keyed by
// idFromName), counting host-creates and join-attempts separately.
export class Limiter {
  constructor(state) {
    this.state = state;
    this.windowStart = 0;
    this.hostCount = 0;
    this.joinCount = 0;
  }

  async fetch(request) {
    const action = new URL(request.url).searchParams.get("action");
    const now = Date.now();
    if (now - this.windowStart > RATE_WINDOW_MS) {
      this.windowStart = now;
      this.hostCount = 0;
      this.joinCount = 0;
    }
    let allowed;
    if (action === "host") allowed = ++this.hostCount <= HOST_LIMIT;
    else allowed = ++this.joinCount <= JOIN_LIMIT;
    return new Response(JSON.stringify({ allowed }),
                        { headers: { "Content-Type": "application/json" } });
  }
}

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
    let ice = [];
    try { ice = JSON.parse(url.searchParams.get("ice") || "[]"); } catch (e) {}
    const now = Date.now();

    if (this.host && now - this.created > ROOM_TTL_MS) this.expire();

    if (url.pathname === "/exists") {
      // Worker's pre-mint probe: is there an open slot worth minting for?
      return new Response(
          JSON.stringify({ host: !!this.host, joiner: !!this.joiner }),
          { headers: { "Content-Type": "application/json" } });
    }

    if (url.pathname === "/host") {
      if (this.host) return new Response("room in use", { status: 409 });
      const pair = new WebSocketPair();
      this.accept_host(pair[1], code, now, ice);
      return new Response(null, { status: 101, webSocket: pair[0] });
    }

    if (url.pathname === "/join") {
      if (!this.host) return this.reject_ws("no-such-room");
      if (this.joiner) return this.reject_ws("room-full");
      const pair = new WebSocketPair();
      this.accept_joiner(pair[1], ice);
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

  send_ice(ws, ice) {
    // One flat frame per server: the game's JSON parser is flat-object only.
    for (const s of ice)
      ws.send(JSON.stringify({ t: "ice", urls: s.urls,
                               username: s.username, credential: s.credential }));
  }

  accept_host(ws, code, now, ice) {
    ws.accept();
    this.host = ws;
    this.offer = null;
    this.created = now;
    this.send_ice(ws, ice);
    ws.send(JSON.stringify({ t: "room", code }));
    ws.addEventListener("message", (m) => this.from_host(m));
    const drop = () => this.expire();
    ws.addEventListener("close", drop);
    ws.addEventListener("error", drop);
  }

  accept_joiner(ws, ice) {
    ws.accept();
    this.joiner = ws;
    this.send_ice(ws, ice);
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
    if (msg.t === "offer" && typeof msg.sdp === "string" &&
        msg.sdp.length <= MAX_SDP_LEN) {
      this.offer = msg.sdp; // kept for a joiner (or rejoiner) arriving later
      if (this.joiner) this.joiner.send(JSON.stringify(msg));
    }
  }

  from_joiner(m) {
    let msg;
    try { msg = JSON.parse(m.data); } catch (e) { return; }
    if (msg.t === "answer" && typeof msg.sdp === "string" &&
        msg.sdp.length <= MAX_SDP_LEN) {
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
