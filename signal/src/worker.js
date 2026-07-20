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
// Counted from room CREATION even while the host socket is live, so it
// must outlive any real co-op session: at the old 2 h a long game
// silently lost its rejoin room mid-session — the host kept showing
// ROOM <code> (expiry is lazy, the host is never told) while every
// join got no-such-room (Glenn, 2026-07-20). Abandoned rooms are
// reaped by host-disconnect + HOST_GRACE_MS regardless, so this TTL
// only backstops leaked-but-connected sockets.
const ROOM_TTL_MS = 24 * 60 * 60 * 1000;

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

// Browser origins allowed to open a signaling socket. Browsers always send an
// Origin header on the WebSocket handshake and cannot forge it, so this stops
// an unrelated web page a player visits from opening role=host in their
// browser and reading the minted TURN credentials off the {t:"ice"} frames
// (denial-of-wallet at scale). Non-browser callers (the native game client)
// send no Origin and were never bound by it — they use their own IP under the
// per-IP mint caps — so a missing Origin is allowed. Entries beginning with a
// dot match that host or any subdomain; others match the full origin exactly.
// Override with the ALLOWED_ORIGINS secret (comma-separated) without a
// redeploy. Origin never carries a path, so path-scoped deploys (…/play/) are
// covered by their bare origin.
const DEFAULT_ALLOWED_ORIGINS = [
  "https://newtonia.metonymous.com", // GitHub Pages web build (served at /play/)
  "https://metonymous.itch.io",      // itch.io project page
  ".itch.zone",                      // itch.io HTML5 game iframe (rotating CDN host)
  ".hwcdn.net",                      // itch.io CDN origin
  ".localhost",                      // wrangler dev + local browser test
  ".127.0.0.1",
];

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

// Is the request's Origin permitted to open a signaling socket? See
// DEFAULT_ALLOWED_ORIGINS. Exported for the unit test.
export function origin_allowed(env, origin) {
  if (!origin) return true; // native client / non-browser caller
  const list = (env.ALLOWED_ORIGINS
      ? env.ALLOWED_ORIGINS.split(",")
      : DEFAULT_ALLOWED_ORIGINS).map((s) => s.trim()).filter(Boolean);
  let hostname;
  try { hostname = new URL(origin).hostname; } catch (e) { return false; }
  return list.some((e) => e.startsWith(".")
      ? (hostname === e.slice(1) || hostname.endsWith(e))
      : origin === e);
}

function random_code() {
  // Rejection sampling for a uniform code: a plain `byte % 28` biases the
  // first 256 % 28 = 4 letters (A-D), since 28 doesn't divide 256. Discard
  // bytes at or above the largest multiple of the alphabet length so every
  // letter is equally likely.
  const n = CODE_ALPHABET.length;
  const limit = Math.floor(256 / n) * n; // 252 for n = 28
  let code = "";
  while (code.length < CODE_LEN) {
    for (const b of crypto.getRandomValues(new Uint8Array(CODE_LEN))) {
      if (b >= limit) continue; // in the biased tail — draw again
      code += CODE_ALPHABET[b % n];
      if (code.length === CODE_LEN) break;
    }
  }
  return code;
}

function turn_ttl(env) {
  return Math.min(Math.max(Number(env.TURN_TTL) || 4 * 60 * 60, 30),
                  48 * 60 * 60);
}

// ---- TURN monthly egress budget ------------------------------------------
// The issuance budget the mint comment always promised: stop minting TURN
// credentials once the account's REAL month-to-date TURN egress (read from
// the GraphQL Analytics API) crosses the budget, so the free account can
// never be billed no matter what any population does. Sessions degrade to
// STUN-only past the cap — direct pairs (the vast majority) are unaffected;
// only the CGNAT relay fallback pauses until the month rolls over.
//
// Default 900 GB = 90% of the Realtime free tier's 1,000 GB/month; override
// with `wrangler secret put TURN_BUDGET_GB`. Needs two extra secrets:
// CF_ACCOUNT_ID (the account tag) and CF_ANALYTICS_TOKEN (an API token with
// Account Analytics: Read). Without them the budget can't be measured and
// minting stays open — the budget is a COST CAP, not an auth gate, and the
// per-IP mint limits still apply.
//
// The reading is cached ~15 min per isolate; a failed refresh keeps the
// last reading (a tripped budget stays tripped, an open one stays open) and
// retries in a minute. Analytics lag + the refresh window bound the
// overshoot past the trip point; the 100 GB of headroom under the free
// tier absorbs it.
const TURN_BUDGET_GB_DEFAULT = 900;
const TURN_BUDGET_RECHECK_MS = 15 * 60 * 1000;
const TURN_BUDGET_RETRY_MS = 60 * 1000;

export function turn_budget_gb(env) {
  const v = Number(env.TURN_BUDGET_GB);
  return Number.isFinite(v) && v > 0 ? v : TURN_BUDGET_GB_DEFAULT;
}

// Month-to-date TURN egress in bytes via the GraphQL Analytics API
// (callsTurnUsageAdaptiveGroups, summed over the current UTC month for our
// key). Returns null when unconfigured, unreachable, or unparseable.
export async function turn_egress_month_bytes(env, fetcher = fetch,
                                              now = new Date()) {
  if (!env.CF_ACCOUNT_ID || !env.CF_ANALYTICS_TOKEN || !env.TURN_KEY_ID)
    return null;
  const y = now.getUTCFullYear();
  const m = String(now.getUTCMonth() + 1).padStart(2, "0");
  const from = `${y}-${m}-01`;
  const to = now.toISOString().slice(0, 10);
  // Values are interpolated inline (none are user input) so the query needs
  // no variable declarations — the Date scalar coerces from the strings.
  const query = `query { viewer { accounts(filter: {accountTag: "${env.CF_ACCOUNT_ID}"}) {
    callsTurnUsageAdaptiveGroups(limit: 100, filter: {
      keyId: "${env.TURN_KEY_ID}", date_geq: "${from}", date_leq: "${to}"
    }) { sum { egressBytes } } } } }`;
  try {
    const resp = await fetcher("https://api.cloudflare.com/client/v4/graphql", {
      method: "POST",
      headers: {
        Authorization: `Bearer ${env.CF_ANALYTICS_TOKEN}`,
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ query }),
    });
    if (!resp.ok) return null;
    const data = await resp.json();
    const accounts = data && data.data && data.data.viewer &&
                     data.data.viewer.accounts;
    const groups = accounts && accounts[0] &&
                   accounts[0].callsTurnUsageAdaptiveGroups;
    if (!Array.isArray(groups)) return null;
    let bytes = 0;
    for (const g of groups) bytes += Number(g && g.sum && g.sum.egressBytes) || 0;
    return bytes;
  } catch (e) {
    return null;
  }
}

// Per-isolate cache of the last reading. Cross-isolate durability is not
// needed: every isolate re-derives the same verdict from the same source of
// truth within one recheck window.
let turn_budget_cache = { bytes: null, at: 0 };
export function turn_budget_cache_reset() {   // tests only
  turn_budget_cache = { bytes: null, at: 0 };
}

export async function turn_budget_ok(env, fetcher = fetch,
                                     now_ms = Date.now()) {
  if (now_ms - turn_budget_cache.at > TURN_BUDGET_RECHECK_MS) {
    const bytes = await turn_egress_month_bytes(env, fetcher);
    if (bytes !== null) {
      turn_budget_cache = { bytes, at: now_ms };
    } else {
      // Keep the last reading; retry soon rather than in a full window.
      turn_budget_cache.at = now_ms - TURN_BUDGET_RECHECK_MS + TURN_BUDGET_RETRY_MS;
    }
  }
  if (turn_budget_cache.bytes === null) return true; // never measured: stay open
  return turn_budget_cache.bytes < turn_budget_gb(env) * 1e9;
}

// Short-lived Cloudflare Calls TURN credentials, minted per connection.
// Configured via secrets (wrangler secret put TURN_KEY_ID / TURN_API_TOKEN);
// without them this returns [] and the game stays STUN-only.
async function turn_ice_servers(env) {
  // TURN kill switch: `wrangler secret put TURN_OFF` cuts the metered relay
  // bandwidth (STUN-only) while signaling keeps working. Delete to restore.
  if (env.TURN_OFF) return [];
  if (!env.TURN_KEY_ID || !env.TURN_API_TOKEN) return [];
  // Automatic monthly budget (see turn_budget_ok above): past the cap the
  // free account must not mint. Same STUN-only degradation as TURN_OFF.
  if (!(await turn_budget_ok(env))) {
    console.log("turn budget tripped — minting paused (STUN-only)");
    return [];
  }
  try {
    const resp = await fetch(
        `https://rtc.live.cloudflare.com/v1/turn/keys/${env.TURN_KEY_ID}/credentials/generate`,
        {
          method: "POST",
          headers: {
            Authorization: `Bearer ${env.TURN_API_TOKEN}`,
            "Content-Type": "application/json",
          },
          // TTL must OUTLIVE the session, not just connection setup: TURN
          // allocations stay alive through periodic Refresh requests, and
          // refreshes authenticate with this credential — when it expires,
          // a relayed session dies within one allocation lifetime
          // (~15-25 min at the old 15-min TTL; the auto-pause/rejoin
          // machinery healed it, but as a mystery hiccup for exactly the
          // players on the worst networks). Neither peer can renew
          // mid-session: libdatachannel has no ICE restart, so renewal
          // would mean a full make-before-break transport swap. 4 h
          // covers any plausible sitting; the harvested-credential risk
          // this lengthens is already bounded by per-IP mint rate limits
          // (and an issuance budget, when added). Cloudflare's cap: 48 h.
          // `wrangler secret put TURN_TTL` overrides (seconds, clamped to
          // Cloudflare's 48 h cap) — for expiry testing with a tiny TTL;
          // delete the secret to restore the default.
          body: JSON.stringify({ ttl: turn_ttl(env) }),
        });
    // Visible in `npx wrangler tail` — the only way to confirm a TURN_TTL
    // override actually took effect for a given mint (the credential
    // itself never exposes its expiry to the game).
    console.log(`turn creds minted, ttl=${turn_ttl(env)}s`);
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

    // Reject cross-site browser callers before spending a limiter round-trip
    // or minting TURN. A blocked page gets a readable {t:"err"} frame.
    if (!origin_allowed(env, request.headers.get("Origin")))
      return reject_ws(request, "forbidden-origin");

    // CF-Connecting-IP may be absent under miniflare (wrangler dev --local);
    // a fixed fallback key still lets the limiter exercise end to end.
    const ip = rate_key(request.headers.get("CF-Connecting-IP") || "local");
    const role = url.searchParams.get("role");

    // Master kill switch: `wrangler secret put DISABLED` (any value) refuses
    // all rooms without a redeploy; `wrangler secret delete DISABLED` restores.
    // A refused host gets an error frame and drops to the manual clipboard
    // code flow (net_lobby fall_back_to_manual), so friends can still play.
    // No TURN is minted on a refused request. Checked before the limiter and
    // before turn_ice_servers so a disabled worker mints nothing and spends
    // no Limiter-DO round-trips.
    if (env.DISABLED)
      return reject_ws(request, role === "join" ? "no-such-room" : "disabled");

    if (role === "host") {
      if (!(await within_limit(env, ip, "host")))
        return reject_ws(request, "rate-limited");
      // Host RECLAIM (M3-1): a disconnected host returning within the
      // grace window proves ownership with the token from its room frame.
      // Validate the code shape, then confirm the token against the room
      // BEFORE minting TURN — a garbage or wrong-token reclaim shouldn't
      // cost a credential (mirrors the /exists pre-check on the join path).
      const token = url.searchParams.get("token");
      if (token) {
        const code = (url.searchParams.get("code") || "").toUpperCase();
        if (code.length !== CODE_LEN ||
            [...code].some((c) => !CODE_ALPHABET.includes(c)))
          return new Response("bad code", { status: 400 });
        const room = env.ROOMS.get(env.ROOMS.idFromName(code));
        let ice = [];
        try {
          const pre = await room.fetch(new Request(
              `https://room/reclaim-check?token=${encodeURIComponent(token)}`));
          const st = await pre.json();
          if (st.ok) ice = await turn_ice_servers(env);
        } catch (e) {}
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
    // The window counters MUST survive DO eviction. An idle Durable Object
    // is evicted and its constructor re-runs on the next request; if these
    // lived only in memory (as they used to), that reset the fixed window
    // and let a client pacing just slower than the eviction interval mint
    // TURN credentials without bound — the cap this DO exists to enforce.
    // So persist to storage and reload here, mirroring the Room DO.
    this.l = { windowStart: 0, hostCount: 0, joinCount: 0 };
    state.blockConcurrencyWhile(async () => {
      const stored = await state.storage.get("limits");
      if (stored) this.l = stored;
    });
  }

  async fetch(request) {
    const action = new URL(request.url).searchParams.get("action");
    const now = Date.now();
    if (now - this.l.windowStart > RATE_WINDOW_MS) {
      this.l.windowStart = now;
      this.l.hostCount = 0;
      this.l.joinCount = 0;
    }
    let allowed;
    if (action === "host") allowed = ++this.l.hostCount <= HOST_LIMIT;
    else allowed = ++this.l.joinCount <= JOIN_LIMIT;
    await this.state.storage.put("limits", this.l);
    // Self-clean: once a full window elapses with no further hits the stored
    // count is meaningless, so free it via the alarm rather than leaving one
    // storage row per IP ever seen (unbounded growth = its own abuse vector).
    await this.state.storage.setAlarm(now + RATE_WINDOW_MS + 1000);
    return new Response(JSON.stringify({ allowed }),
                        { headers: { "Content-Type": "application/json" } });
  }

  async alarm() {
    if (Date.now() - this.l.windowStart > RATE_WINDOW_MS)
      await this.state.storage.deleteAll();
  }
}

// One Durable Object per room code. Uses the WebSocket HIBERNATION API:
// after the handshake burst the signaling socket sits idle for the whole
// game session (traffic is peer-to-peer over WebRTC), so the DO evicts
// between messages and stops billing wall-clock for idle held sockets.
//
// Because the DO can be evicted, in-memory fields don't survive — room
// state (offer/token/grace/closed) lives in DO storage (this.r, loaded in
// the constructor), and the host/joiner sockets are recovered by tag via
// getWebSockets(). Timers are a single storage alarm (grace/TTL cleanup);
// correctness still rides the lazy expiry checks in fetch().
export class Room {
  constructor(state) {
    this.state = state;
    this.r = { offer: null, host_cands: [], host_token: null,
               host_lost_at: 0, created: 0, closed: false };
    state.blockConcurrencyWhile(async () => {
      const stored = await state.storage.get("room");
      if (stored) this.r = stored;
    });
  }

  async save() { await this.state.storage.put("room", this.r); }

  // Live sockets by tag (hibernation-safe — survives DO eviction).
  hostWs()   { return this.state.getWebSockets("host")[0]   || null; }
  joinerWs() { return this.state.getWebSockets("joiner")[0] || null; }

  safeSend(ws, obj) { try { ws.send(JSON.stringify(obj)); } catch (e) {} }

  // The room is "held" while a disconnected host may still reclaim it.
  in_grace(now) {
    return !this.hostWs() && this.r.host_token &&
           this.r.host_lost_at && now - this.r.host_lost_at <= HOST_GRACE_MS;
  }
  alive(now) { return !!this.hostWs() || this.in_grace(now); }

  async fetch(request) {
    const url = new URL(request.url);
    const code = url.searchParams.get("code");
    let ice = [];
    try { ice = JSON.parse(url.searchParams.get("ice") || "[]"); } catch (e) {}
    const now = Date.now();

    if (this.alive(now) && this.r.created && now - this.r.created > ROOM_TTL_MS)
      await this.expire();
    // Grace elapsed with no reclaim: the room is finished.
    if (!this.hostWs() && this.r.host_token && !this.in_grace(now))
      await this.expire();

    if (url.pathname === "/exists") {
      // Worker's pre-mint probe: is there an open slot worth minting for?
      return new Response(
          JSON.stringify({ host: !!this.hostWs(), joiner: !!this.joinerWs() }),
          { headers: { "Content-Type": "application/json" } });
    }

    if (url.pathname === "/host") {
      // In-grace rooms still own their code — a fresh host must not
      // squat a room whose original host may return.
      if (this.hostWs() || this.in_grace(now))
        return new Response("room in use", { status: 409 });
      const pair = new WebSocketPair();
      await this.accept_host(pair[1], code, now, ice);
      return new Response(null, { status: 101, webSocket: pair[0] });
    }

    if (url.pathname === "/reclaim-check") {
      // Worker's pre-mint probe: would this token actually reclaim the room?
      // Same predicate as /reclaim below, minus the socket upgrade, so TURN
      // is minted only for a reclaim that will succeed. The real /reclaim
      // re-checks, so a state change in the gap is refused there (no leak).
      // A lingering host socket (abrupt drop, close not yet detected) still
      // yields ok: the reclaim below evicts it, so TURN must be minted.
      const token = url.searchParams.get("token");
      const ok = this.r.host_token && token === this.r.host_token &&
                 (this.hostWs() || this.in_grace(now));
      return new Response(JSON.stringify({ ok: !!ok }),
                          { headers: { "Content-Type": "application/json" } });
    }

    if (url.pathname === "/reclaim") {
      const token = url.searchParams.get("token");
      // A valid host token proves ownership. A host returning after an
      // ABRUPT drop (wifi off, laptop sleep) routinely finds its OLD socket
      // still registered — the DO hasn't seen the TCP close yet — so
      // hostWs() is truthy and grace never started. Rejecting that as
      // "room-in-use" stranded the rightful host on CONNECTION LOST (Glenn:
      // reclaim after wifi-off). Accept whenever the token matches and the
      // room isn't truly gone (a live-or-stale host socket, or still in
      // grace); reclaim_host evicts any lingering socket.
      if (!this.r.host_token || token !== this.r.host_token)
        return this.reject_ws("no-such-room");
      if (!this.hostWs() && !this.in_grace(now))
        return this.reject_ws("no-such-room");
      const pair = new WebSocketPair();
      await this.reclaim_host(pair[1], code, ice);
      return new Response(null, { status: 101, webSocket: pair[0] });
    }

    if (url.pathname === "/join") {
      // Joining is allowed while the host is in grace too: the joiner
      // waits and the reclaimed host's fresh offer is relayed on arrival.
      if (!this.alive(now))
        return this.reject_ws(this.r.closed ? "host-closed" : "no-such-room");
      if (this.joinerWs()) return this.reject_ws("room-full");
      const pair = new WebSocketPair();
      await this.accept_joiner(pair[1], ice);
      return new Response(null, { status: 101, webSocket: pair[0] });
    }

    return new Response("not found", { status: 404 });
  }

  // Refusals still complete the WS upgrade so the client gets a readable
  // {t:"err"} frame. Ephemeral socket, closed at once — not hibernated.
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
      this.safeSend(ws, { t: "ice", urls: s.urls,
                          username: s.username, credential: s.credential });
  }

  async accept_host(ws, code, now, ice) {
    this.state.acceptWebSocket(ws, ["host"]);
    this.r = { offer: null, host_cands: [], host_token: crypto.randomUUID(),
               host_lost_at: 0, created: now, closed: false };
    await this.save();
    await this.state.storage.setAlarm(now + ROOM_TTL_MS);
    this.send_ice(ws, ice);
    this.safeSend(ws, { t: "room", code, token: this.r.host_token });
  }

  // Reclaim: same room, same token, fresh socket. The old offer is stale
  // (its transport died with the old socket); the host re-offers.
  async reclaim_host(ws, code, ice) {
    // Evict any lingering host socket first: an abrupt drop can leave the
    // old one registered (its TCP close undetected) until now. The valid
    // token that got us here proves this new socket is the rightful owner.
    // The old socket's later webSocketClose is a no-op — drop_host sees this
    // fresh socket and bails as "superseded".
    for (const old of this.state.getWebSockets("host")) {
      try { old.close(1000); } catch (e) {}
    }
    this.state.acceptWebSocket(ws, ["host"]);
    this.r.offer = null;
    this.r.offer_pv = null;
    this.r.host_cands = [];
    this.r.host_lost_at = 0;
    await this.save();
    this.send_ice(ws, ice);
    this.safeSend(ws, { t: "room", code, token: this.r.host_token });
    const j = this.joinerWs();
    if (j) this.safeSend(j, { t: "peer", ev: "host-back" });
  }

  async accept_joiner(ws, ice) {
    this.state.acceptWebSocket(ws, ["joiner"]);
    this.send_ice(ws, ice);
    this.safeSend(ws, { t: "joined" });
    const h = this.hostWs();
    if (h) this.safeSend(h, { t: "peer", ev: "join" });
    if (this.r.offer) {
      const replay = { t: "offer", sdp: this.r.offer };
      if (this.r.offer_pv) replay.pv = this.r.offer_pv;
      this.safeSend(ws, replay);
      // Trickle: candidates the host gathered before this joiner arrived.
      for (const c of this.r.host_cands) this.safeSend(ws, c);
    }
  }

  // ---- hibernation event handlers ----------------------------------------

  async webSocketMessage(ws, message) {
    if (typeof message !== "string") return;  // frames are JSON text
    let msg;
    try { msg = JSON.parse(message); } catch (e) { return; }
    const tags = this.state.getTags(ws);
    if (tags.includes("host")) await this.from_host(msg);
    else if (tags.includes("joiner")) await this.from_joiner(msg);
  }

  async webSocketClose(ws) { await this.drop(ws); }
  async webSocketError(ws) { await this.drop(ws); }

  async drop(ws) {
    const tags = this.state.getTags(ws);
    if (tags.includes("host")) await this.drop_host(ws);
    else if (tags.includes("joiner")) await this.drop_joiner(ws);
  }

  async from_host(msg) {
    // Deliberate host teardown (quit to menu, game over): the room dies
    // NOW — no reclaim grace, no joiners left waiting on a host that told
    // us it isn't coming back. Crashes send nothing, so grace still
    // covers real drops.
    if (msg.t === "close") { await this.expire("host-closed"); return; }
    if (msg.t === "offer" && typeof msg.sdp === "string" &&
        msg.sdp.length <= MAX_SDP_LEN) {
      this.r.offer = msg.sdp; // kept for a joiner (or rejoiner) arriving later
      // Version stamp (pv): stored with the offer so the replay to a
      // late-arriving joiner carries it too — the game fails fast on a
      // mismatch before ICE. Old games send no pv.
      this.r.offer_pv =
        typeof msg.pv === "string" && msg.pv.length <= 8 ? msg.pv : null;
      this.r.host_cands = []; // fresh offer = fresh transport = old cands stale
      await this.save();
      const j = this.joinerWs();
      if (j) this.safeSend(j, msg);
    }
    // Trickle ICE (M3-2b): relay candidates. Only BUFFER (persist) them
    // when there's no joiner yet — the buffer exists solely to replay to a
    // joiner arriving mid-gather, so once a joiner is present each
    // candidate is relayed live and persisting it is pure cost (it was
    // re-serializing the whole room record, offer SDP included, per
    // candidate). Candidates after the joiner arrives don't touch storage.
    if (msg.t === "cand" && typeof msg.cand === "string" &&
        msg.cand.length <= MAX_CAND_LEN) {
      const frame = { t: "cand", mid: String(msg.mid || "0"), cand: msg.cand };
      const j = this.joinerWs();
      if (j) {
        this.safeSend(j, frame);
      } else if (this.r.host_cands.length < MAX_CANDS) {
        this.r.host_cands.push(frame);
        await this.save();
      }
    }
  }

  async from_joiner(msg) {
    if (msg.t === "answer" && typeof msg.sdp === "string" &&
        msg.sdp.length <= MAX_SDP_LEN) {
      const h = this.hostWs();
      if (h) this.safeSend(h, msg);
    }
    if (msg.t === "cand" && typeof msg.cand === "string" &&
        msg.cand.length <= MAX_CAND_LEN) {
      // No buffering: the host is connected whenever a joiner exists (or
      // in grace, when its dead transport couldn't use them anyway).
      const h = this.hostWs();
      if (h) this.safeSend(h, { t: "cand", mid: String(msg.mid || "0"), cand: msg.cand });
    }
  }

  // Host socket gone: start the reclaim grace window instead of killing
  // the room. Idempotent — webSocketError then webSocketClose may both fire.
  async drop_host(ws) {
    if (this.r.host_lost_at && !this.hostWs()) return;   // already in grace
    if (this.state.getWebSockets("host").some((w) => w !== ws)) return; // superseded
    this.r.offer = null;
    this.r.offer_pv = null;
    this.r.host_cands = [];
    this.r.host_lost_at = Date.now();
    await this.save();
    await this.state.storage.setAlarm(this.r.host_lost_at + HOST_GRACE_MS);
    const j = this.joinerWs();
    if (j) this.safeSend(j, { t: "peer", ev: "host-lost" });
  }

  async drop_joiner(ws) {
    if (this.state.getWebSockets("joiner").some((w) => w !== ws)) return;
    // The stored offer likely belongs to a transport that joiner was
    // party to; replaying it to a REjoiner dead-ends the handshake. The
    // host re-offers on peer-leave (lobby) or on its rejoin flow (game),
    // and that fresh offer is stored and replayed instead.
    this.r.offer = null;
    this.r.offer_pv = null;
    this.r.host_cands = [];
    await this.save();
    const h = this.hostWs();
    if (h) this.safeSend(h, { t: "peer", ev: "leave" });
  }

  // Close all sockets and reset room state. A host-close leaves a "closed"
  // tombstone so a rejoiner learns the host left (until the cleanup alarm
  // frees the code); other expiries reset to a plain dead room.
  async expire(reason) {
    const bye = JSON.stringify({ t: "err", reason: reason || "expired" });
    for (const ws of this.state.getWebSockets()) {
      try { ws.send(bye); ws.close(1000); } catch (e) {}
    }
    this.r = { offer: null, host_cands: [], host_token: null,
               host_lost_at: 0, created: 0, closed: reason === "host-closed" };
    await this.save();
    await this.state.storage.setAlarm(Date.now() + HOST_GRACE_MS);
  }

  // Storage-alarm backstop: proactively free abandoned rooms (grace/TTL
  // elapsed, or a host-closed tombstone past its window) so their DO
  // storage doesn't linger. Lazy expiry in fetch() covers correctness;
  // this is just cleanup, and re-arms while the room is still live.
  async alarm() {
    const now = Date.now();
    if (this.alive(now)) {
      if (this.r.created)
        await this.state.storage.setAlarm(this.r.created + ROOM_TTL_MS);
      return;
    }
    for (const ws of this.state.getWebSockets()) {
      try { ws.send(JSON.stringify({ t: "err", reason: "expired" })); ws.close(1000); } catch (e) {}
    }
    this.r = { offer: null, host_cands: [], host_token: null,
               host_lost_at: 0, created: 0, closed: false };
    await this.state.storage.deleteAll();
  }
}
