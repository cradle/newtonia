// Newtonia netplay signaling — room codes + WebRTC offer/answer relay.
// See NETPLAY.md (Milestone 2). One Durable Object instance per room code;
// the worker only mints codes and routes WebSocket upgrades to the room.
//
// Wire protocol (JSON text frames, all fields lowercase):
//   host  -> /ws?role=host            {t:"room", code}        assigned code
//   join  -> /ws?role=join&code=ABCDE {t:"joined"}             or {t:"err",reason}
//   host  -> {t:"offer", sdp[, to]}   addressed (`to` = joiner id) offers go
//                                     to that joiner and are stored per-jid;
//                                     an UNADDRESSED offer keeps the legacy
//                                     single-pair semantics — it goes to the
//                                     oldest connected joiner, or is stored
//                                     and replayed once to the next arrival
//   join  -> {t:"answer", sdp}        relayed to the host, stamped {from: jid}
//   both  -> {t:"cand", mid, cand[, to]}  trickle ICE, same addressing rules;
//                                     joiner->host cands are stamped {from}
//   host  <- {t:"peer", ev:"join"|"leave", from}
//   join  <- {t:"peer", ev:"host-lost"|"host-back"}   (broadcast to all)
//   any   <- {t:"err", reason:"no-such-room"|"room-full"|"expired"}
//
// Rooms hold one host and up to MAX_JOINERS joiners (FOURPLAYER.md PB-D5).
// Each joiner socket is tagged ["joiner", "j:<n>"] with a monotonically
// increasing per-room id — tags are the only per-socket state that survives
// DO hibernation, so the jid lives there and is never reused, which is what
// keeps the late-verify race (attest_identity) simple. A joiner's slot
// reopens when its socket disconnects; the room dies when the host
// disconnects past its grace or after ROOM_TTL_MS.

import { verifySteamTicket } from "./steam_verify.js";
import { verifyPlayGamesCode } from "./play_games_verify.js";
import { verifyGameCenterCred } from "./game_center_verify.js";

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
// Env-overridable (RATE_HOST_LIMIT / RATE_JOIN_LIMIT) so the wrangler-dev
// protocol tests — ten-plus rooms and dozens of joins in one window from
// one IP — can raise them without touching production defaults.
const HOST_LIMIT = 10;
const JOIN_LIMIT = 30;

// PB-D5: joiners per room (host + 3 = the 4-player ceiling). The GAME's own
// seat cap (NET_PLAYER_CAP) stays authoritative below this — a 2-player
// build's host simply never offers to a third joiner, which then waits and
// backs out on its own lobby timeout.
const MAX_JOINERS = 3;

// SDP frames are relayed verbatim into DO memory; cap them so a peer can't
// pin unbounded memory or flood the relay. Oversized frames are dropped.
const MAX_SDP_LEN = 16384;
// Trickle ICE candidate lines (M3-2b): one line each, and a session
// produces a handful — the caps bound a flooding peer.
const MAX_CAND_LEN = 512;
const MAX_CANDS = 32;

// Peer identity attestation (NETPLAY.md V0/V1). Each side announces its
// claimed platform + display name (and, on Steam, a Web-API auth ticket) with
// an {t:"identity"} frame; the worker verifies the credential against the
// platform backend and broadcasts an {t:"identity", role, platform, name,
// verified} frame to the peer. The display-name cap mirrors the game's
// NET_IDENTITY_NAME_MAX so a truncation can't disagree across the wire.
const MAX_IDENTITY_NAME = 24;
// Credential (Steam ticket hex) cap — see steam_verify.MAX_TICKET_HEX; bound
// here too so an oversized frame is dropped before any verification work.
const MAX_IDENTITY_CRED = 8192;
// Min interval between Valve verifications for a given role (denial-of-wallet
// guard — see attest_identity). Well under any legitimate re-verify cadence
// (join, rejoin, host reclaim — attestation is one-shot per session, the
// periodic heartbeat being declined in NETPLAY.md V1.5), so it only trims
// floods.
const VERIFY_MIN_INTERVAL_MS = 3000;

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
        if (st.host && !st.full) ice = await turn_ice_servers(env);
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
  constructor(state, env) {
    this.state = state;
    this.env = env;  // test-only limit overrides (see HOST_LIMIT)
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
    const host_limit = Number(this.env && this.env.RATE_HOST_LIMIT) || HOST_LIMIT;
    const join_limit = Number(this.env && this.env.RATE_JOIN_LIMIT) || JOIN_LIMIT;
    let allowed;
    if (action === "host") allowed = ++this.l.hostCount <= host_limit;
    else allowed = ++this.l.joinCount <= join_limit;
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
  constructor(state, env) {
    this.state = state;
    this.env = env;  // for the identity verifier's platform API key / dev flag
    this.r = { offer: null, host_cands: [], host_token: null,
               host_lost_at: 0, created: 0, closed: false,
               // The host's last attested identity, kept for replay to a
               // joiner that arrives after attestation.
               host_identity: null,
               // PB-D5 per-joiner state. next_jid is the monotonic id mint
               // (never reused — see the module header); jids[jid] holds
               // {identity, verify_at, claim_at, epoch} and outlives its
               // socket when a VERIFIED identity was attested (a host
               // reclaim replays it — socket closed != player gone, the
               // game rides the independent WebRTC transport; unverified
               // claims are reaped so a claim flood can't grow the record
               // past the DO storage value cap). Addressed offers/cands
               // are relay-only — a jid's socket is connected for its
               // whole life, so there is nothing to buffer for it; the
               // legacy offer/host_cands slot above keeps the unaddressed
               // 2P semantics.
               next_jid: 1, jids: {} };
    state.blockConcurrencyWhile(async () => {
      const stored = await state.storage.get("room");
      // Spread over the defaults: a room stored by the pre-multi-join
      // worker lacks the per-jid fields and must not resurrect undefined.
      if (stored) this.r = { next_jid: 1, jids: {}, ...stored };
    });
  }

  async save() { await this.state.storage.put("room", this.r); }

  // Live sockets by tag (hibernation-safe — survives DO eviction). Every
  // joiner carries BOTH the generic "joiner" tag (dispatch) and its own
  // "j:<n>" tag (addressing).
  hostWs()   { return this.state.getWebSockets("host")[0]   || null; }
  joinerWss() { return this.state.getWebSockets("joiner"); }
  joinerWsById(jid) {
    return this.state.getWebSockets("j:" + jid)[0] || null;
  }
  // The oldest connected joiner — the unaddressed-frame target (legacy 2P
  // hosts know only one peer). Jids are monotonic, so oldest = smallest; a
  // socket accepted by the PRE-multi-join worker carries no jid tag
  // (deploy-moment survivor) and counts as oldest of all.
  oldestJoinerWs() {
    let best = null, best_jid = Infinity;
    for (const ws of this.joinerWss()) {
      const jid = this.jidOf(ws);
      const k = jid === null ? 0 : jid;
      if (k < best_jid) { best_jid = k; best = ws; }
    }
    return best;
  }
  jidOf(ws) {
    for (const tag of this.state.getTags(ws))
      if (tag.startsWith("j:")) return Number(tag.slice(2));
    return null;
  }

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
      // `joiner` (any-connected) kept for shape compatibility; the worker
      // reads `full` since multi-join.
      const n = this.joinerWss().length;
      return new Response(
          JSON.stringify({ host: !!this.hostWs(), joiner: n > 0,
                           joiners: n, full: n >= MAX_JOINERS }),
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
      //
      // Tombstone FIRST, before any socket-liveness heuristic: ws.close()
      // is async, so the host socket expire("host-closed") just closed can
      // linger in getWebSockets() for a beat — alive() then read a
      // deliberately closed room as live and ACCEPTED the joiner into it,
      // where they sat in silence (no host, no offer, and no err ever
      // sent). The host said it left; no lingering socket outranks that.
      // A fresh /host on the code resets closed, so this refuses nothing
      // legitimately re-hosted. Caught by host_close_broadcast_test.mjs's
      // late join, which lost this race twice in five runs on the loaded
      // CI runners (2026-08-14) while passing everywhere locally.
      if (this.r.closed || !this.alive(now))
        return this.reject_ws(this.r.closed ? "host-closed" : "no-such-room");
      if (this.joinerWss().length >= MAX_JOINERS)
        return this.reject_ws("room-full");
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

  // Per-jid identity bookkeeping (identity/verify_at/claim_at/epoch).
  // Created on first touch; identity-BEARING entries outlive their socket
  // (reclaim replay), identity-less disconnected ones are pruned on the
  // next accept_joiner so a churny room can't grow the record unbounded.
  jid_entry(jid) {
    if (!this.r.jids[jid])
      this.r.jids[jid] = { identity: null, verify_at: 0, claim_at: 0, epoch: 0 };
    return this.r.jids[jid];
  }

  // ---- peer identity attestation (NETPLAY.md V0/V1) ----------------------
  // A client announced {t:"identity", platform, name, cred?}. Bound the
  // claimed fields, verify the credential against the platform backend, store
  // the attested result for replay, and push it to the peers. Verification
  // NEVER rejects the room: a failure attests nothing (verified:false) and
  // the peers render a role label. `jid` names the announcing joiner (null
  // for the host); all its bookkeeping lives in this.r.jids[jid].
  async attest_identity(role, msg, jid) {
    let platform = Number(msg.platform);
    if (!Number.isInteger(platform) || platform < 0 || platform > 255)
      platform = 0;
    const name = typeof msg.name === "string"
        ? msg.name.slice(0, MAX_IDENTITY_NAME) : "";
    const cred = typeof msg.cred === "string" &&
                 msg.cred.length <= MAX_IDENTITY_CRED ? msg.cred : "";

    let attested = { platform, name, verified: false };
    const fake = this.env && this.env.FAKE_VERIFY === "1";
    const fake_slow = fake && /^SLOW(FAIL)?:\d+$/.test(cred);
    if (fake && !fake_slow) {
      // Dev/e2e shortcut (wrangler dev only): attest the claim without
      // contacting any platform backend. NEVER set in production. Compared
      // to "1" exactly (the value every harness passes): a plain truthiness
      // test made FAKE_VERIFY=0 ENABLE the fake — the footgun class where
      // setting a flag "off" turns it on.
      attested = { platform, name, verified: true };
    } else if (cred) {
      // Per-platform verifier: each proves the account against the platform's
      // own backend and returns the display NAME to attest (the wire name is
      // never trusted). An unknown/unverifiable platform has no verifier and
      // stays a claim.
      //   Steam   (2): Web-API auth ticket -> AuthenticateUserTicket + persona.
      //   iOS     (4): Game Center id signature -> Apple cert verify (account
      //                only; Apple exposes no server-side alias lookup, so the
      //                attested name is always empty — see game_center_verify.js).
      //   Android (5): Play Games server auth code -> token exchange + player.
      let verify = null;
      if (fake_slow)
        // Dev/e2e slow-verify hook (FAKE_VERIFY-gated like the fast path,
        // so it can never run in production): a cred of "SLOW:<ms>" /
        // "SLOWFAIL:<ms>" runs the FULL async verify machinery — throttle,
        // epoch capture, the vacant-slot late path — with an injected delay
        // standing in for the platform round-trip, succeeding/failing the
        // verify respectively. identity_test.js races it against a socket
        // close.
        verify = async () => {
          const delay_ms = Math.min(Number(cred.split(":")[1]) || 0, 5000);
          await new Promise((res) => setTimeout(res, delay_ms));
          return cred.startsWith("SLOWFAIL") ? null : { name };
        };
      else if (platform === 2 /* NET_PLATFORM_STEAM */)
        verify = async () => {
          const v = await verifySteamTicket(this.env, cred);
          return v ? { name: v.persona || "" } : null;
        };
      else if (platform === 4 /* NET_PLATFORM_IOS */)
        verify = async () => {
          const v = await verifyGameCenterCred(this.env, cred);
          // Log WHICH (identifier, digest) Apple actually signed — the device
          // test's answer to the open M3-4 question (greppable in wrangler tail).
          if (v) console.log(`game center verified idKind=${v.idKind} hash=${v.hash}`);
          // Account proven, name unavailable from Apple: attest the empty name
          // (platform ATTESTED, name ABSENT -> peer renders "PLAYER N - IOS").
          return v ? { name: "" } : null;
        };
      else if (platform === 5 /* NET_PLATFORM_ANDROID */)
        verify = async () => {
          const v = await verifyPlayGamesCode(this.env, cred);
          return v ? { name: v.name || "" } : null;
        };
      if (verify) {
        // Denial-of-wallet guard: every verify is a round-trip against the
        // platform backend's budget/quota. The per-IP connect limiter bounds
        // NEW sockets but not per-message floods over an already-open one, and
        // WS frames never pass through the fetch-handler Limiter — so throttle
        // the backend call per role here. Legit re-verifies (host reclaim, a
        // client rejoin) are seconds-to-minutes apart; only a flood is
        // dropped, and a dropped frame KEEPS the last attestation (no demote).
        const now = Date.now();
        const jent = role === "joiner" ? this.jid_entry(jid) : null;
        const at = jent ? jent.verify_at : this.r.host_verify_at || 0;
        if (at && now - at < VERIFY_MIN_INTERVAL_MS)
          return;  // flooded: keep the prior attestation, spend no backend call
        // PERSIST the throttle stamp BEFORE the backend call: this DO hibernates
        // between idle messages, and an unpersisted stamp would reset on
        // eviction — a peer pacing frames across evictions could then defeat
        // the guard. Saving first makes it durable.
        if (jent) jent.verify_at = now; else this.r.host_verify_at = now;
        await this.save();
        // Occupancy epoch, captured before the verify: the input gate is
        // open across the non-storage await, so the announcing socket can
        // drop while the platform round-trip is in flight. drop_joiner/
        // drop_host bump the epoch. For the HOST role a mismatch means the
        // socket flapped — the reclaimed socket re-announces anyway, so a
        // cross-flap verify is discarded. For a JOINER the jid makes this
        // simple (PB-D5): jids are never reused, so a bumped epoch can only
        // mean the announcer ITSELF departed — never a replacement in its
        // slot — and the account just proven is still the player the peers
        // are mid-game with over the (independent) WebRTC transport (the
        // field-hit fast-ICE case, 2026-08-07). Store the attestation (a
        // host reclaim replays it) and push it to the peers now.
        const epoch = jent ? jent.epoch : this.r.host_id_epoch || 0;
        const v = await verify();
        // The room can be torn down (host `close`, TTL/grace expiry) while the
        // verify fetch is in flight — the input gate is open across a non-storage
        // await. Don't write identity back onto a dead/tombstoned room.
        if (this.r.closed || !this.r.host_token) return;
        const jent2 = role === "joiner" ? this.r.jids[jid] : null;
        const epoch_now = role === "joiner" ? (jent2 ? jent2.epoch : -1)
                                            : this.r.host_id_epoch || 0;
        if (epoch_now !== epoch) {
          if (role === "host" || !v || !jent2) {
            console.log(`identity ${role} verify discarded (announcer gone ` +
                        `mid-verify)`);
            return;
          }
          jent2.identity = { platform, name: v.name || "", verified: true };
          await this.save();
          this.broadcast_identity(role, jid);
          console.log(`identity joiner j:${jid} late verify attested after ` +
                      `announcer closed mid-verify`);
          return;
        }
        if (v) {
          // Attested name comes from the platform, not the wire (a lying name
          // field stops mattering); the account proven is enough to badge.
          attested = { platform, name: v.name || "", verified: true };
        }
        // Verify failed (bad/expired/reused credential, backend down): fall
        // through to the never-demote guard below, which keeps the badge.
      }
    }

    // Never demote a verified badge. An unverified re-announce reaches here
    // three ways — a failed backend verify, a no-verifier platform, and a
    // credential-LESS frame (a host reclaim re-announces its identity, and
    // the warmed single-use credential may be gone by then) — and none of
    // them may overwrite verified:true with verified:false: keep the stored
    // attestation and re-push it so a reclaimed peer still hears it.
    const ent = role === "joiner" ? this.jid_entry(jid) : null;
    const prev = ent ? ent.identity : this.r.host_identity;
    if (!attested.verified && prev && prev.verified) {
      // Rate-limited on the claim stamp: the re-push exists for a
      // reclaimed peer's credential-less re-announce, which happens once —
      // an unthrottled repeat was a free relay-amplification lever.
      const rnow = Date.now();
      const rat = ent ? ent.claim_at : this.r.host_claim_at || 0;
      if (rat && rnow - rat < VERIFY_MIN_INTERVAL_MS) return;
      if (ent) ent.claim_at = rnow; else this.r.host_claim_at = rnow;
      await this.save();
      this.broadcast_identity(role, jid);
      console.log(`identity ${role} kept verified attestation ` +
                  `(unverified re-announce, credlen=${cred.length})`);
      return;
    }
    if (!attested.verified) {
      // Unverified claims bypass the backend-verify throttle above but
      // still cost a billed storage put (the whole room record) plus a
      // relay per frame — the same per-message flood gap the verify
      // throttle was added for. An identical repeat claim is free to drop
      // (nothing to store, the peers already heard it / replay covers late
      // joiners); a CHANGED claim is rate-limited per announcer on its own
      // stamp, so a claim flood can't meter-spin storage writes while a
      // later credentialed verify (separate stamp) is unaffected.
      if (prev && !prev.verified && prev.platform === attested.platform &&
          prev.name === attested.name)
        return;
      const cnow = Date.now();
      const cat = ent ? ent.claim_at : this.r.host_claim_at || 0;
      if (cat && cnow - cat < VERIFY_MIN_INTERVAL_MS)
        return;  // flooded: keep the prior claim, spend no storage write
      if (ent) ent.claim_at = cnow; else this.r.host_claim_at = cnow;
    }
    if (ent) ent.identity = attested; else this.r.host_identity = attested;
    await this.save();
    this.broadcast_identity(role, jid);
    // credlen distinguishes "client sent no credential" (credlen=0 — e.g. the
    // iOS simulator not issuing an identity-verification signature) from
    // "credential sent but rejected" (credlen>0 with verified=false — a real
    // verifier/data issue worth debugging).
    console.log(`identity ${role} platform=${attested.platform} ` +
                `verified=${attested.verified} credlen=${cred.length}`);
  }

  // PB-D5 identity fan-out. The host's attestation goes to every joiner; a
  // joiner's goes to the HOST only, stamped with its jid ({from}) since
  // "joiner" alone no longer identifies who. Joiner badges are NOT fanned
  // to other joiners yet — 2P clients apply identity frames role-blind, so
  // another joiner's badge would overwrite the host's on their screen; B4's
  // roster consumer re-adds that leg. No-op for peers not connected yet —
  // the stored copy is replayed on join/reclaim instead.
  identity_frame(role, jid) {
    const id = role === "host" ? this.r.host_identity
                               : (this.r.jids[jid] || {}).identity;
    if (!id) return null;
    const frame = { t: "identity", role, platform: id.platform,
                    name: id.name, verified: id.verified };
    if (role === "joiner") frame.from = String(jid);
    return frame;
  }

  broadcast_identity(role, jid) {
    const frame = this.identity_frame(role, jid);
    if (!frame) return;
    if (role === "host") {
      for (const j of this.joinerWss()) this.safeSend(j, frame);
    } else {
      const h = this.hostWs();
      if (h) this.safeSend(h, frame);
    }
  }

  // Replay one stored attestation to a socket that just (re)joined.
  replay_identity(ws, role, jid) {
    const frame = this.identity_frame(role, jid);
    if (frame) this.safeSend(ws, frame);
  }

  async accept_host(ws, code, now, ice) {
    this.state.acceptWebSocket(ws, ["host"]);
    this.r = { offer: null, offer_pv: null, host_cands: [],
               host_token: crypto.randomUUID(),
               host_lost_at: 0, created: now, closed: false,
               host_identity: null,
               next_jid: 1, jids: {} };
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
    // The stored offer belonged to the dead socket's transports; the
    // host re-offers on the fresh one.
    this.r.offer = null;
    this.r.offer_pv = null;
    this.r.host_cands = [];
    this.r.host_lost_at = 0;
    await this.save();
    this.send_ice(ws, ice);
    this.safeSend(ws, { t: "room", code, token: this.r.host_token });
    for (const j of this.joinerWss())
      this.safeSend(j, { t: "peer", ev: "host-back" });
    // The reclaimed host missed any identity the joiners sent while it was
    // gone; replay every jid's (including late-attested departed ones — the
    // player is still mid-game over WebRTC). The host re-announces its own
    // on the fresh Room frame.
    for (const jid of Object.keys(this.r.jids))
      this.replay_identity(ws, "joiner", jid);
  }

  async accept_joiner(ws, ice) {
    // Mint the jid and carry it in the socket's tags — the only per-socket
    // state that survives hibernation. Never reused (see the header), which
    // is what keeps the late-verify race one-sided: a fresh player can
    // never inherit a departed jid's badge.
    const jid = this.r.next_jid;
    this.r.next_jid = jid + 1;
    this.state.acceptWebSocket(ws, ["joiner", "j:" + jid]);
    this.jid_entry(jid);
    // Prune entries whose socket is gone and whose identity isn't a
    // VERIFIED attestation: keeps the room record bounded under churn — an
    // unverified claim costs nothing to mint, so claim-then-leave loops
    // must not accrete storage toward the DO's per-value cap. Verified
    // entries stay for the reclaim replay, and a recent verify_at means a
    // platform round-trip may still be in flight for the departed jid (the
    // late-attest path) — give it well past the verify window first.
    const prune_now = Date.now();
    for (const k of Object.keys(this.r.jids)) {
      const e = this.r.jids[k];
      if ((!e.identity || !e.identity.verified) && Number(k) !== jid &&
          !this.joinerWsById(k) &&
          (!e.verify_at || prune_now - e.verify_at > 15000))
        delete this.r.jids[k];
    }
    await this.save();
    this.send_ice(ws, ice);
    this.safeSend(ws, { t: "joined" });
    const h = this.hostWs();
    if (h) this.safeSend(h, { t: "peer", ev: "join", from: String(jid) });
    // Replay the legacy unaddressed offer slot — CONSUMED on delivery: the
    // SDP is single-use, so a second joiner must never receive the same
    // one. (Addressed offers are relay-only; a fresh jid can't have one.)
    if (this.r.offer) {
      const replay = { t: "offer", sdp: this.r.offer };
      if (this.r.offer_pv) replay.pv = this.r.offer_pv;
      this.safeSend(ws, replay);
      // Trickle: candidates the host gathered before this joiner arrived.
      for (const c of this.r.host_cands) this.safeSend(ws, c);
      this.r.offer = null;
      this.r.offer_pv = null;
      this.r.host_cands = [];
      await this.save();
    }
    // Replay the host's attested identity ONLY. Joiner badges are not
    // fanned to other joiners yet: today's 2P clients apply identity
    // frames role-blind (the single attested_peer_ slot), so a replayed
    // joiner badge — including a rejoiner's OWN — would overwrite the
    // host's on their screen. B4's lobby roster adds the per-jid consumer
    // and the fan-out with it.
    this.replay_identity(ws, "host");
  }

  // ---- hibernation event handlers ----------------------------------------

  async webSocketMessage(ws, message) {
    if (typeof message !== "string") return;  // frames are JSON text
    let msg;
    try { msg = JSON.parse(message); } catch (e) { return; }
    // A valid-JSON but non-object frame ("null", "5", "\"x\"") would make the
    // handlers read `.t` off a non-object — `null.t` throws. Only dispatch
    // real objects; everything else is an ill-formed frame, dropped.
    if (!msg || typeof msg !== "object") return;
    const tags = this.state.getTags(ws);
    if (tags.includes("host")) await this.from_host(msg);
    else if (tags.includes("joiner")) await this.from_joiner(msg, this.jidOf(ws));
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
    if (msg.t === "identity") { await this.attest_identity("host", msg); return; }
    // A well-formed addressed frame carries to:"<digits>" (PB-D5); anything
    // else is the legacy unaddressed form.
    const to = typeof msg.to === "string" && /^\d{1,6}$/.test(msg.to)
        ? Number(msg.to) : null;
    if (msg.t === "offer" && typeof msg.sdp === "string" &&
        msg.sdp.length <= MAX_SDP_LEN) {
      // Version stamp (pv): stored with the offer so the replay to a
      // late-arriving joiner carries it too — the game fails fast on a
      // mismatch before ICE. Old games send no pv.
      const pv = typeof msg.pv === "string" && msg.pv.length <= 8 ? msg.pv : null;
      if (to !== null) {
        // Addressed (per-jid) offer: relay-only. A jid's socket is
        // connected for its whole life (the host can only learn a jid
        // from its join event), so a disconnected target means the jid
        // departed — the offer is stale by definition and storing it
        // would only grow the room record (a host re-offers to the
        // REPLACEMENT jid on its join event).
        const j = this.joinerWsById(to);
        if (j) this.safeSend(j, msg);
      } else {
        // Legacy unaddressed offer (2P hosts): to the oldest connected
        // joiner NOW — consumed, not stored, since an SDP is single-use —
        // or stored for a one-shot replay to the next arrival.
        const j = this.oldestJoinerWs();
        if (j) {
          this.safeSend(j, msg);
          if (this.r.offer) {
            this.r.offer = null;
            this.r.offer_pv = null;
            this.r.host_cands = [];
            await this.save();
          }
        } else {
          this.r.offer = msg.sdp;
          this.r.offer_pv = pv;
          this.r.host_cands = [];
          await this.save();
        }
      }
    }
    // Trickle ICE (M3-2b): relay candidates. Only BUFFER (persist) them
    // when the target isn't connected yet — the buffer exists solely to
    // replay to a joiner arriving mid-gather, so once it is present each
    // candidate is relayed live and persisting it is pure cost (it was
    // re-serializing the whole room record, offer SDP included, per
    // candidate). Candidates after the target arrives don't touch storage.
    if (msg.t === "cand" && typeof msg.cand === "string" &&
        msg.cand.length <= MAX_CAND_LEN) {
      const frame = { t: "cand", mid: String(msg.mid || "0"), cand: msg.cand };
      if (to !== null) {
        // Relay-only, like the addressed offer above.
        const j = this.joinerWsById(to);
        if (j) this.safeSend(j, frame);
      } else {
        const j = this.oldestJoinerWs();
        if (j) {
          this.safeSend(j, frame);
        } else if (this.r.host_cands.length < MAX_CANDS) {
          this.r.host_cands.push(frame);
          await this.save();
        }
      }
    }
  }

  async from_joiner(msg, jid) {
    if (msg.t === "identity") {
      // jid null = a pre-multi-join socket surviving the deploy; its
      // announce would key the map under "null" — skip (the badge falls
      // to the role label for that bounded transition window).
      if (jid !== null) await this.attest_identity("joiner", msg, jid);
      return;
    }
    // Joiner frames are stamped {from: jid} for the host (PB-D5) — the
    // joiner itself never learns its jid; the worker derives it from the
    // socket's tag, so old joiners need no change. A null jid (pre-deploy
    // socket) forwards unstamped, exactly the old frames.
    if (msg.t === "answer" && typeof msg.sdp === "string" &&
        msg.sdp.length <= MAX_SDP_LEN) {
      const h = this.hostWs();
      if (h) this.safeSend(h, jid === null ? msg : { ...msg, from: String(jid) });
    }
    if (msg.t === "cand" && typeof msg.cand === "string" &&
        msg.cand.length <= MAX_CAND_LEN) {
      // No buffering: the host is connected whenever a joiner exists (or
      // in grace, when its dead transport couldn't use them anyway).
      const h = this.hostWs();
      if (h) {
        const frame = { t: "cand", mid: String(msg.mid || "0"), cand: msg.cand };
        if (jid !== null) frame.from = String(jid);
        this.safeSend(h, frame);
      }
    }
  }

  // Host socket gone: start the reclaim grace window instead of killing
  // the room. Idempotent — webSocketError then webSocketClose may both fire.
  async drop_host(ws) {
    if (this.r.host_lost_at && !this.hostWs()) return;   // already in grace
    if (this.state.getWebSockets("host").some((w) => w !== ws)) return; // superseded
    // Every stored offer belonged to the dead socket's transports.
    this.r.offer = null;
    this.r.offer_pv = null;
    this.r.host_cands = [];
    this.r.offers = {};
    this.r.host_lost_at = Date.now();
    // Invalidate any host verify still in flight (see attest_identity's
    // epoch check). Reclaim requires the token, so the occupant can't
    // actually change accounts — but the reclaimed socket re-announces and
    // re-attests anyway, so discarding a cross-flap verify costs nothing.
    this.r.host_id_epoch = (this.r.host_id_epoch || 0) + 1;
    await this.save();
    await this.state.storage.setAlarm(this.r.host_lost_at + HOST_GRACE_MS);
    for (const j of this.joinerWss())
      this.safeSend(j, { t: "peer", ev: "host-lost" });
  }

  async drop_joiner(ws) {
    const jid = this.jidOf(ws);
    if (jid === null) {
      // A socket accepted by the PRE-multi-join worker (no jid tag, alive
      // across the deploy): give it the old drop semantics — clear the
      // legacy offer slot when it was the last joiner and tell the host,
      // unstamped, so the lobby's re-offer flow still runs.
      if (this.joinerWss().length === 0) {
        this.r.offer = null;
        this.r.offer_pv = null;
        this.r.host_cands = [];
        await this.save();
      }
      const h0 = this.hostWs();
      if (h0) this.safeSend(h0, { t: "peer", ev: "leave" });
      return;
    }
    // Superseded: another socket already carries THIS jid's tag (jids are
    // never reused, so this can only be error-then-close double delivery).
    if (this.state.getWebSockets("j:" + jid).some((w) => w !== ws)) return;
    // The legacy offer slot is cleared when this was the only joiner — the
    // 2P flow this room may be serving; the host re-offers on peer-leave
    // (lobby) or its rejoin flow (game). Addressed offers are relay-only,
    // so there is no per-jid buffer to clear.
    if (this.joinerWss().length === 0) {
      this.r.offer = null;
      this.r.offer_pv = null;
      this.r.host_cands = [];
    }
    // Bump the jid's epoch: invalidates any verify still in flight enough
    // for attest_identity to notice the departure (the late path may still
    // attest it to this jid — never to a replacement, since jids are not
    // reused). An UNVERIFIED identity dies with the socket (a claim is
    // free to mint, so keeping it would let claim-then-leave churn grow
    // the room record without bound); verified ones stay for the host
    // reclaim replay.
    const ent = this.r.jids[jid];
    if (ent) {
      ent.epoch = (ent.epoch || 0) + 1;
      if (ent.identity && !ent.identity.verified) ent.identity = null;
    }
    await this.save();
    const h = this.hostWs();
    if (h) this.safeSend(h, { t: "peer", ev: "leave", from: String(jid) });
  }

  // Close all sockets and reset room state. A host-close leaves a "closed"
  // tombstone so a rejoiner learns the host left (until the cleanup alarm
  // frees the code); other expiries reset to a plain dead room.
  async expire(reason) {
    const bye = JSON.stringify({ t: "err", reason: reason || "expired" });
    for (const ws of this.state.getWebSockets()) {
      try { ws.send(bye); ws.close(1000); } catch (e) {}
    }
    this.r = { offer: null, offer_pv: null, host_cands: [],
               host_token: null, host_lost_at: 0, created: 0,
               closed: reason === "host-closed", host_identity: null,
               next_jid: 1, jids: {} };
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
    this.r = { offer: null, offer_pv: null, host_cands: [],
               host_token: null, host_lost_at: 0, created: 0, closed: false,
               host_identity: null, next_jid: 1, jids: {} };
    await this.state.storage.deleteAll();
  }
}
