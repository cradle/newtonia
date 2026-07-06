// Newtonia netplay signaling — room codes + WebRTC offer/answer relay.
// See NETPLAY.md (Milestone 2). One Durable Object instance per room code;
// the worker only mints codes and routes WebSocket upgrades to the room.
//
// Wire protocol (JSON text frames, all fields lowercase):
//   host  -> /ws?role=host            {t:"room", code}        assigned code
//   join  -> /ws?role=join&code=ABCDE {t:"joined"}             or {t:"err",reason}
//   host  -> {t:"offer", sdp}         relayed to the joiner (also replayed to
//                                     a joiner who arrives after the offer)
//   join  -> {t:"answer", sdp}        relayed to the host
//   both  <- {t:"peer", ev:"join"|"leave"}
//   any   <- {t:"err", reason:"no-such-room"|"room-full"|"expired"}
//
// Rooms hold exactly one host and at most one joiner. The joiner slot
// reopens when the joiner disconnects (Milestone 2 rejoin); the room dies
// when the host disconnects or after ROOM_TTL_MS.

const CODE_ALPHABET = "ABCDEGHJKLMNPQRTUVWXYZ2346789"; // no 0/O/1/I/5/S (confusable in the game font) or F (game fullscreen key)
const CODE_LEN = 5;
const ROOM_TTL_MS = 2 * 60 * 60 * 1000;

// Per-IP rate limits (fixed window). Host-creates farm rooms + TURN
// credentials; join-attempts can brute-force the 4-letter code space.
const RATE_WINDOW_MS = 10 * 60 * 1000;

// M3-1: when the HOST's socket drops (mobile app backgrounded, wifi blip)
// the room survives for a grace period instead of dying; the host reclaims
// its slot with the token minted at room creation.
const HOST_GRACE_MS = 2 * 60 * 1000;
const HOST_LIMIT = 10;
const JOIN_LIMIT = 30;

// SDP frames are relayed verbatim into DO memory; cap them so a peer can't
// pin unbounded memory or flood the relay. Oversized frames are dropped.
const MAX_SDP_LEN = 16384;
// Trickle ICE candidate lines (M3-2b): one line each, and a session
// produces a handful — the caps bound a flooding peer.
const MAX_CAND_LEN = 512;
const MAX_CANDS = 32;

// Rate-limit key from the client IP. IPv6 users get a whole /64 (or more)
// to rotate through for free, which would bypass a per-address limit — so
// collapse v6 to its /64 prefix; v4 addresses key whole. Exported for the
// unit test.
export function rate_key(ip) {
  if (ip.includes(":")) {
    // Expand :: then keep the first 4 hextets (the routable /64).
    const parts = ip.split("::");
    let head = parts[0] ? parts[0].split(":") : [];
    let tail = parts.length > 1 && parts[1] ? parts[1].split(":") : [];
    const fill = 8 - head.length - tail.length;
    const full = head.concat(Array(Math.max(0, fill)).fill("0"), tail);
    return "v6:" + full.slice(0, 4).join(":");
  }
  return "v4:" + ip;
}

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
          // Short TTL: ICE only needs the credential during connection
          // setup (an established relay allocation outlives it). Keeps a
          // harvested credential nearly worthless as free relay bandwidth.
          body: JSON.stringify({ ttl: 15 * 60 }),
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
    const ip = rate_key(request.headers.get("CF-Connecting-IP") || "local");
    const role = url.searchParams.get("role");

    if (role === "host") {
      if (!(await within_limit(env, ip, "host")))
        return reject_ws(request, "rate-limited");
      // Host RECLAIM (M3-1): a disconnected host returning within the
      // grace window proves ownership with the token from its room frame.
      // Validate the code shape BEFORE minting TURN — a garbage reclaim
      // shouldn't cost a credential.
      const token = url.searchParams.get("token");
      if (token) {
        const code = (url.searchParams.get("code") || "").toUpperCase();
        if (code.length !== CODE_LEN ||
            [...code].some((c) => !CODE_ALPHABET.includes(c)))
          return new Response("bad code", { status: 400 });
        const ice = await turn_ice_servers(env);
        const room = env.ROOMS.get(env.ROOMS.idFromName(code));
        return room.fetch(new Request(
            `https://room/reclaim?code=${code}&token=${encodeURIComponent(token)}&ice=${encodeURIComponent(JSON.stringify(ice))}`,
            request));
      }
      // Fresh host: passing the limiter is the authorization for this
      // attempt. Missing TURN secrets still yield [] (STUN-only).
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
    this.host_cands = []; // trickle candidates buffered alongside the offer
    this.created = 0;
    this.host_token = null;   // reclaim proof, minted at creation
    this.host_lost_at = 0;    // grace window start (0 = host connected)
    this.closed = false;      // host sent {t:"close"} — deliberate shutdown
  }

  // The room is "held" while a disconnected host may still reclaim it.
  in_grace(now) {
    return !this.host && this.host_token &&
           this.host_lost_at && now - this.host_lost_at <= HOST_GRACE_MS;
  }

  // A live room = host connected, or host within its reclaim grace.
  alive(now) {
    return !!this.host || this.in_grace(now);
  }

  async fetch(request) {
    const url = new URL(request.url);
    const code = url.searchParams.get("code");
    let ice = [];
    try { ice = JSON.parse(url.searchParams.get("ice") || "[]"); } catch (e) {}
    const now = Date.now();

    if (this.alive(now) && now - this.created > ROOM_TTL_MS) this.expire();
    // Grace elapsed with no reclaim: the room is finished.
    if (!this.host && this.host_token && !this.in_grace(now)) this.expire();

    if (url.pathname === "/exists") {
      // Worker's pre-mint probe: is there an open slot worth minting for?
      return new Response(
          JSON.stringify({ host: !!this.host, joiner: !!this.joiner }),
          { headers: { "Content-Type": "application/json" } });
    }

    if (url.pathname === "/host") {
      // In-grace rooms still own their code — a fresh host must not
      // squat a room whose original host may return.
      if (this.host || this.in_grace(now))
        return new Response("room in use", { status: 409 });
      const pair = new WebSocketPair();
      this.accept_host(pair[1], code, now, ice);
      return new Response(null, { status: 101, webSocket: pair[0] });
    }

    if (url.pathname === "/reclaim") {
      const token = url.searchParams.get("token");
      if (this.host) return this.reject_ws("room-in-use");
      if (!this.host_token || token !== this.host_token || !this.in_grace(now))
        return this.reject_ws("no-such-room");
      const pair = new WebSocketPair();
      this.reclaim_host(pair[1], code, ice);
      return new Response(null, { status: 101, webSocket: pair[0] });
    }

    if (url.pathname === "/join") {
      // Joining is allowed while the host is in grace too: the joiner
      // waits and the reclaimed host's fresh offer is relayed on arrival.
      if (!this.alive(now))
        return this.reject_ws(this.closed ? "host-closed" : "no-such-room");
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
    this.host_cands = [];
    this.created = now;
    this.host_token = crypto.randomUUID();
    this.host_lost_at = 0;
    this.closed = false;  // a fresh host reopens a deliberately-closed code
    this.send_ice(ws, ice);
    ws.send(JSON.stringify({ t: "room", code, token: this.host_token }));
    ws.addEventListener("message", (m) => this.from_host(m));
    const drop = () => this.drop_host(ws);
    ws.addEventListener("close", drop);
    ws.addEventListener("error", drop);
  }

  // Reclaim: same room, same token, fresh socket. The old offer is stale
  // (its transport died with the old socket); the host re-offers.
  reclaim_host(ws, code, ice) {
    ws.accept();
    this.host = ws;
    this.offer = null;
    this.host_cands = [];
    this.host_lost_at = 0;
    this.send_ice(ws, ice);
    ws.send(JSON.stringify({ t: "room", code, token: this.host_token }));
    ws.addEventListener("message", (m) => this.from_host(m));
    const drop = () => this.drop_host(ws);
    ws.addEventListener("close", drop);
    ws.addEventListener("error", drop);
    if (this.joiner)
      this.joiner.send(JSON.stringify({ t: "peer", ev: "host-back" }));
  }

  // Host socket gone: start the reclaim grace window instead of killing
  // the room. The stale-socket guard matters: a reclaimed host's OLD
  // socket may emit its close event after the new one attached.
  drop_host(ws) {
    if (this.host !== ws) return;
    this.host = null;
    this.offer = null;
    this.host_cands = [];
    this.host_lost_at = Date.now();
    if (this.joiner)
      this.joiner.send(JSON.stringify({ t: "peer", ev: "host-lost" }));
  }

  accept_joiner(ws, ice) {
    ws.accept();
    this.joiner = ws;
    this.send_ice(ws, ice);
    ws.send(JSON.stringify({ t: "joined" }));
    if (this.host) this.host.send(JSON.stringify({ t: "peer", ev: "join" }));
    if (this.offer) {
      ws.send(JSON.stringify({ t: "offer", sdp: this.offer }));
      // Trickle: candidates the host gathered before this joiner arrived.
      for (const c of this.host_cands) ws.send(JSON.stringify(c));
    }
    ws.addEventListener("message", (m) => this.from_joiner(m));
    const drop = () => this.drop_joiner(ws);
    ws.addEventListener("close", drop);
    ws.addEventListener("error", drop);
  }

  from_host(m) {
    let msg;
    try { msg = JSON.parse(m.data); } catch (e) { return; }
    // Deliberate host teardown (quit to menu, game over): the room dies
    // NOW — no reclaim grace, no joiners left waiting on a host that told
    // us it isn't coming back. Crashes send nothing, so grace still
    // covers real drops.
    if (msg.t === "close") {
      this.closed = true;  // joins now get "host-closed", not "no-such-room"
      this.expire("host-closed");
      return;
    }
    if (msg.t === "offer" && typeof msg.sdp === "string" &&
        msg.sdp.length <= MAX_SDP_LEN) {
      this.offer = msg.sdp; // kept for a joiner (or rejoiner) arriving later
      this.host_cands = []; // fresh offer = fresh transport = old cands stale
      if (this.joiner) this.joiner.send(JSON.stringify(msg));
    }
    // Trickle ICE (M3-2b): relay candidates; buffer the host's so a joiner
    // arriving mid-gather gets them replayed right after the offer.
    if (msg.t === "cand" && typeof msg.cand === "string" &&
        msg.cand.length <= MAX_CAND_LEN) {
      const frame = { t: "cand", mid: String(msg.mid || "0"), cand: msg.cand };
      if (this.host_cands.length < MAX_CANDS) this.host_cands.push(frame);
      if (this.joiner) this.joiner.send(JSON.stringify(frame));
    }
  }

  from_joiner(m) {
    let msg;
    try { msg = JSON.parse(m.data); } catch (e) { return; }
    if (msg.t === "answer" && typeof msg.sdp === "string" &&
        msg.sdp.length <= MAX_SDP_LEN) {
      if (this.host) this.host.send(JSON.stringify(msg));
    }
    if (msg.t === "cand" && typeof msg.cand === "string" &&
        msg.cand.length <= MAX_CAND_LEN) {
      // No buffering: the host is connected whenever a joiner exists (or
      // in grace, when its dead transport couldn't use them anyway).
      if (this.host)
        this.host.send(JSON.stringify(
            { t: "cand", mid: String(msg.mid || "0"), cand: msg.cand }));
    }
  }

  drop_joiner(ws) {
    if (this.joiner !== ws) return;
    this.joiner = null;
    // The stored offer likely belongs to a transport that joiner was
    // party to; replaying it to a REjoiner dead-ends the handshake. The
    // host re-offers on peer-leave (lobby) or on its rejoin flow (game),
    // and that fresh offer is stored and replayed instead.
    this.offer = null;
    this.host_cands = [];
    if (this.host) this.host.send(JSON.stringify({ t: "peer", ev: "leave" }));
  }

  expire(reason) {
    const bye = JSON.stringify({ t: "err", reason: reason || "expired" });
    for (const ws of [this.host, this.joiner]) {
      if (!ws) continue;
      try { ws.send(bye); ws.close(1000); } catch (e) {}
    }
    this.host = null;
    this.joiner = null;
    this.offer = null;
    this.host_cands = [];
    this.host_token = null;
    this.host_lost_at = 0;
  }
}
