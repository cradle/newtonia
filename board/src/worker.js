// Newtonia leaderboard worker (LEADERBOARD.md L1): seasonal score table in
// D1, replay blobs in R2, all traffic over one WebSocket endpoint at
// /board (JSON control frames + binary chunks — the game has no HTTP
// client on native, so everything speaks the same WS the signaling stack
// already proves on every platform).
//
// Wire protocol (client -> / worker <-):
//   -> {t:"qualify", season, players, score}
//   <- {t:"qualify", place, cutline, would_place}
//   -> {t:"top", season, players, count}
//   <- {t:"top", rows:[{rank,name,platform,verified,score,generation,
//                       duration_ms,date,has_replay,run_id,
//                       format,save_format}]}
//   -> {t:"seasons"}
//   <- {t:"seasons", rows:[{season,newest,count}]}   newest-first, max 50
//   -> {t:"rank-of", season, players, score}
//   <- {t:"rank-of", place}
//   -> {t:"submit", size, platform, name, cred}   attestation REQUIRED
//   <- {t:"submit-ok"}          then client sends binary chunks (<=64 KB)
//   -> {t:"submit-end"}
//   <- {t:"placed", rank}       or {t:"err", reason}
//   -> {t:"fetch", season, run_id}
//   <- {t:"fetch-ok", size} + binary chunks + {t:"fetch-end"}
//   any error: {t:"err", reason}
//
// Admission (LEADERBOARD.md decisions): the blob is the submission — score,
// season, players and run_id come from the parsed header, never from a
// claimed field; FLAG_CLEAN required, FLAG_CHEATED rejected; and the
// submitter's platform credential MUST verify (no unattested replays). The
// verify modules are imported from the signal worker — one implementation,
// two workers.

import { verifySteamTicket } from "../../signal/src/steam_verify.js";
import { verifyPlayGamesCode } from "../../signal/src/play_games_verify.js";
import { verifyGameCenterCred } from "../../signal/src/game_center_verify.js";
import {
  validate_submission,
  season_ok,
  MAX_SUBMISSION_BYTES,
  MIN_SUBMISSION_BYTES,
} from "./validate.js";

// Board size: rows at rank <= KEEP_N keep their replay blob; the retention
// cron demotes the rest to score-only (LEADERBOARD.md retention decision).
// (Not exported: workerd only allows function/class exports from the entry
// module — a bare number export fails the deploy with "Incorrect type for
// map entry".)
const KEEP_N = 100;
// Seasons with no submission newer than this lose all blobs (score-only) —
// except the live season (newest submission per players count), which is
// never dormancy-stripped; see scheduled().
const SCORE_ONLY_AFTER_MS = 180 * 24 * 60 * 60 * 1000;

// Replay-download chunk size (server -> client). Kept well under 16 KB to
// mirror the client's upload chunks: field evidence showed a ~60 KB binary
// frame silently vanishing on the real edge in the upload direction
// (net_board_rtc.cpp CHUNK), and the download direction gets the same
// safety margin rather than waiting to find out.
const CHUNK = 15 * 1024;

// Display-name bound — mirrors the signal worker's MAX_IDENTITY_NAME and
// the game's NET_IDENTITY_NAME_MAX so no truncation disagreement exists.
const MAX_NAME = 24;
const MAX_CRED = 8192;

// Per-connection budgets (in-DO): a socket that exhausts one is closed.
// These bound a SINGLE connection but reset on reconnect (each connection
// is a fresh Session DO), so they are NOT the aggregate cost bound — that
// is the per-IP Limiter below, which reads and fetches now also pass
// through (a fresh Session DO per connection otherwise let one IP multiply
// D1 reads past the free-tier ceiling — connections × per-conn budget).
const CONN_MAX_QUERIES = 120;
const CONN_MAX_SUBMITS = 2;
const CONN_MAX_FETCHES = 5;
// Cap the reassembled-upload chunk COUNT, not just the byte total: a
// 1-byte-frame flood otherwise pins tens of millions of tiny typed-array
// views (pointer array + per-object overhead) far past the 32 MB size cap.
// Legit clients use 15 KB chunks (~2200 for a 32 MB max); 4096 is generous.
const MAX_UPLOAD_CHUNKS = 4096;
// Idle sockets are closed after this long (per-connection DO — nothing to
// hibernate for, the DO dies with the socket).
const CONN_IDLE_MS = 10 * 60 * 1000;

// Per-IP fixed windows (Limiter DO). Connects gate new sockets; reads and
// fetches are limited in aggregate so connection churn can't multiply
// D1/R2 cost past the free tier (query worst case: 200 × 100 rows =
// 20k rows / 10 min / IP ≈ 2.9M rows/day, under the 5M/day D1 free ceiling
// even before other traffic); submits stay the strict one. NOTE: like the
// signal worker this keys on the client IP (IPv6 collapsed to /64), so a
// large IPv6 allocation can still spread load — an accepted limitation
// shared with signal, not a per-IP-defeatable gap.
// SUBMIT_LIMIT is a dev/test var (wrangler dev --var SUBMIT_LIMIT:100) so
// the protocol test's burst of submissions from one IP doesn't trip the
// production window; never set in production.
const LIMITS = {
  conn: { window_ms: 10 * 60 * 1000, limit: 60 },
  query: { window_ms: 10 * 60 * 1000, limit: 200 },
  fetch: { window_ms: 60 * 60 * 1000, limit: 40 },
  submit: { window_ms: 60 * 60 * 1000, limit: 6 },
};

function limit_for(env, action) {
  const cfg = LIMITS[action] || LIMITS.conn;
  if (action === "submit" && env && Number(env.SUBMIT_LIMIT) > 0)
    return { ...cfg, limit: Number(env.SUBMIT_LIMIT) };
  return cfg;
}

// v1 has no web client (LEADERBOARD.md: no leaderboard on web in the first
// release), so browser origins are refused except local dev. Native
// clients send no Origin and pass. ALLOWED_ORIGINS secret overrides
// without a redeploy (comma-separated; leading dot = subdomain match).
const DEFAULT_ALLOWED_ORIGINS = [".localhost", ".127.0.0.1"];

export function origin_allowed(env, origin) {
  if (!origin) return true; // native client
  const list = (env.ALLOWED_ORIGINS
      ? env.ALLOWED_ORIGINS.split(",")
      : DEFAULT_ALLOWED_ORIGINS).map((s) => s.trim()).filter(Boolean);
  let hostname;
  try { hostname = new URL(origin).hostname; } catch (e) { return false; }
  return list.some((e) => e.startsWith(".")
      ? (hostname === e.slice(1) || hostname.endsWith(e))
      : origin === e);
}

// Same v6-collapse as the signal worker: a /64 rotates for free, so key it
// whole. (Duplicated rather than imported — signal/src/worker.js would
// drag its DO classes into this script's registry.)
export function rate_key(ip) {
  if (ip.includes(":")) {
    const parts = ip.split("::");
    const head = parts[0] ? parts[0].split(":") : [];
    const tail = parts.length > 1 && parts[1] ? parts[1].split(":") : [];
    const fill = 8 - head.length - tail.length;
    const full = head.concat(Array(Math.max(0, fill)).fill("0"), tail);
    return "v6:" + full.slice(0, 4).join(":");
  }
  return "v4:" + ip;
}

function strip_name(name) {
  if (typeof name !== "string") return "";
  let out = "";
  for (const c of name.slice(0, MAX_NAME)) {
    const b = c.charCodeAt(0);
    if (b >= 0x20 && b !== 0x7f) out += c;
  }
  return out;
}

// Verify the submitter's platform credential. Returns null (rejected) or
// {platform, name, verified, account} — `account` is the platform-scoped
// id the platform_key is derived from; `verified` says whether the NAME is
// platform-attested (iOS proves the account but Apple exposes no alias
// lookup, so its claimed alias stays unverified — LEADERBOARD.md).
// A verify that never answers must not wedge the submit: the client sees
// nothing (no submit-ok, no err — field report: "stuck at UPLOADING") and
// the socket just hangs. Race the platform call against a deadline and
// treat a timeout as unverified — the client's unverified retry path then
// handles it like any other stale-credential refusal.
const VERIFY_TIMEOUT_MS = 10 * 1000;
function with_timeout(p, platform) {
  let timer;
  const gate = new Promise((resolve) => {
    timer = setTimeout(() => {
      console.log(`verify timeout platform=${platform}`);
      resolve(null);
    }, VERIFY_TIMEOUT_MS);
  });
  return Promise.race([p.finally(() => clearTimeout(timer)), gate]);
}

export async function verify_identity(env, platform, name, cred) {
  const claimed = strip_name(name);
  if (typeof cred !== "string" || !cred || cred.length > MAX_CRED) return null;
  if (env.FAKE_VERIFY) {
    // Dev/e2e shortcut (wrangler dev only): attest the claim without a
    // platform backend — but still REQUIRE a non-empty credential (above),
    // like every real backend, so the client's empty-at-submit case tests
    // true. The cred is logged so the retry e2e can assert the resubmit
    // carried a genuinely different one. The account derives from the name
    // so two test "players" stay distinct. NEVER set in production.
    console.log(`fake-verify: cred=${cred.slice(0, 64)}`);
    return { platform, name: claimed, verified: true,
             account: `fake:${claimed || "anon"}` };
  }
  if (platform === 2 /* NET_PLATFORM_STEAM */) {
    const v = await with_timeout(verifySteamTicket(env, cred), platform);
    if (!v) return null;
    return { platform, name: strip_name(v.persona || ""), verified: true,
             account: `steam:${v.steamid}` };
  }
  if (platform === 4 /* NET_PLATFORM_IOS */) {
    const v = await with_timeout(verifyGameCenterCred(env, cred), platform);
    if (!v) return null;
    return { platform, name: claimed, verified: false,
             account: `gc:${v.identifier}` };
  }
  if (platform === 5 /* NET_PLATFORM_ANDROID */) {
    const v = await with_timeout(verifyPlayGamesCode(env, cred), platform);
    if (!v) return null;
    return { platform, name: strip_name(v.name || ""), verified: true,
             account: `pg:${v.playerId}` };
  }
  return null; // no verifier for this platform => no admission
}

async function platform_key(account) {
  const data = new TextEncoder().encode(account);
  const hash = await crypto.subtle.digest("SHA-256", data);
  return [...new Uint8Array(hash)]
      .map((b) => b.toString(16).padStart(2, "0")).join("");
}

// ---- D1 ------------------------------------------------------------------

let schema_ready = false; // per-isolate; CREATE IF NOT EXISTS is idempotent
async function ensure_schema(db) {
  if (schema_ready) return;
  await db.batch([
    db.prepare(
        `CREATE TABLE IF NOT EXISTS scores(
           season TEXT NOT NULL, players INTEGER NOT NULL,
           run_id TEXT NOT NULL, score INTEGER NOT NULL,
           generation INTEGER NOT NULL, duration_ms INTEGER NOT NULL,
           submitted_at INTEGER NOT NULL, name TEXT NOT NULL,
           platform INTEGER NOT NULL, verified INTEGER NOT NULL,
           platform_key TEXT NOT NULL, blob_key TEXT NOT NULL,
           format INTEGER NOT NULL DEFAULT 0,
           save_format INTEGER NOT NULL DEFAULT 0,
           PRIMARY KEY (season, run_id))`),
    db.prepare(`CREATE INDEX IF NOT EXISTS scores_rank
                ON scores(season, players, score DESC)`),
    // UNIQUE so the one-row-per-player invariant is enforced by the
    // DATABASE, not just the procedural check-then-act in finish_submit:
    // two concurrent submits from one account can no longer both land
    // (the second's INSERT conflicts, and the ON CONFLICT upsert keeps
    // the best), and no code path can leave a player with two rows.
    db.prepare(`CREATE UNIQUE INDEX IF NOT EXISTS scores_player_uk
                ON scores(season, players, platform_key)`),
  ]);
  // Append-only migrations for tables that predate a column (fresh CREATEs
  // above already carry them): ADD COLUMN throws when the column exists,
  // and swallowing that is the idempotence. Rows from before the migration
  // keep the DEFAULT 0 = "format unknown", which clients treat as "let
  // playback decide".
  for (const ddl of [
    `ALTER TABLE scores ADD COLUMN format INTEGER NOT NULL DEFAULT 0`,
    `ALTER TABLE scores ADD COLUMN save_format INTEGER NOT NULL DEFAULT 0`,
  ]) {
    try { await db.prepare(ddl).run(); } catch (e) {}
  }
  schema_ready = true;
}

// Actual rank of a row already in the table: strictly-better rows + 1.
// (Ties share this optimistic rank; the deterministic submitted_at tiebreak
// `top` uses can only push a tied row DOWN, never up, so this never
// over-reports for a charting check.)
async function rank_of(db, season, players, score) {
  const r = await db.prepare(
      `SELECT COUNT(*) AS n FROM scores
       WHERE season = ?1 AND players = ?2 AND score > ?3`)
      .bind(season, players, score).first();
  return (r ? Number(r.n) : 0) + 1;
}

// Projected rank of a NOT-yet-submitted run: ties count as above it (a new
// submission sorts last within its tie under `top`'s score DESC,
// submitted_at ASC), so `qualify`/`rank-of` never promise a charting rank
// that the listing then contradicts at the cutline.
async function projected_rank(db, season, players, score) {
  const r = await db.prepare(
      `SELECT COUNT(*) AS n FROM scores
       WHERE season = ?1 AND players = ?2 AND score >= ?3`)
      .bind(season, players, score).first();
  return (r ? Number(r.n) : 0) + 1;
}

// The lowest score currently holding a charting (blob-keeping) rank, or
// null while the board has fewer than KEEP_N rows.
async function cutline(db, season, players) {
  const r = await db.prepare(
      `SELECT score FROM scores WHERE season = ?1 AND players = ?2
       ORDER BY score DESC LIMIT 1 OFFSET ?3`)
      .bind(season, players, KEEP_N - 1).first();
  return r ? Number(r.score) : null;
}

function blob_key_for(season, run_id) {
  return `${season}/${run_id}.nrp`;
}

// ---- worker entry --------------------------------------------------------

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname !== "/board")
      return new Response("newtonia-board", { status: 200 });
    if (request.headers.get("Upgrade") !== "websocket")
      return new Response("expected websocket", { status: 426 });
    if (!origin_allowed(env, request.headers.get("Origin")))
      return reject_ws("forbidden-origin");
    // Kill switch, same shape as the signal worker's.
    if (env.DISABLED) return reject_ws("disabled");

    const ip = rate_key(request.headers.get("CF-Connecting-IP") || "local");
    if (!(await within_limit(env, ip, "conn")))
      return reject_ws("rate-limited");

    // One DO per connection: isolates state, and DO message events get
    // their own CPU allotment (a 32 MB reassembly would flirt with the
    // free plan's per-invocation budget in a stateless worker socket).
    const session = env.SESSIONS.get(env.SESSIONS.newUniqueId());
    return session.fetch(new Request(
        `https://session/connect?ip=${encodeURIComponent(ip)}`, request));
  },

  // Retention cron (LEADERBOARD.md): demote rows below KEEP_N to
  // score-only, and strip blobs from seasons whose newest submission is
  // older than SCORE_ONLY_AFTER_MS — EXCEPT the live season. The live
  // season per players count (the one with the newest submission anywhere)
  // is what players are actually browsing, so its top-KEEP_N replays stay
  // watchable no matter how long the game goes quiet; the dormancy strip
  // exists to reclaim R2 from seasons a release has left BEHIND, not to
  // age out the board still on screen. The live season still gets the
  // ordinary below-KEEP_N trim.
  async scheduled(event, env) {
    await ensure_schema(env.DB);
    const boards = await env.DB.prepare(
        `SELECT DISTINCT season, players FROM scores`).all();
    // The live season for each players count.
    const live = new Map();
    for (const players of [1, 2]) {
      const r = await env.DB.prepare(
          `SELECT season FROM scores WHERE players = ?1
           ORDER BY submitted_at DESC LIMIT 1`).bind(players).first();
      if (r) live.set(players, r.season);
    }
    let demoted = 0;
    for (const b of boards.results || []) {
      const stale = live.get(b.players) === b.season
          ? null
          : await env.DB.prepare(
          `SELECT newest FROM (SELECT MAX(submitted_at) AS newest FROM scores
             WHERE season = ?1 AND players = ?2)
           WHERE newest < ?3`)
          .bind(b.season, b.players, Date.now() - SCORE_ONLY_AFTER_MS).first();
      const rows = stale
          ? await env.DB.prepare(
              `SELECT run_id, blob_key FROM scores
               WHERE season = ?1 AND players = ?2 AND blob_key != ''`)
              .bind(b.season, b.players).all()
          : await env.DB.prepare(
              // Rank among ALL rows, not just blob-bearing ones: the OFFSET
              // must skip the top KEEP_N of the FULL board, otherwise a
              // blob row whose true rank is > KEEP_N keeps its blob whenever
              // score-only rows sit above it. Filter blob_key in JS after
              // the offset (a WHERE blob_key != '' before OFFSET would
              // reintroduce the bug it replaces).
              `SELECT run_id, blob_key FROM scores
               WHERE season = ?1 AND players = ?2
               ORDER BY score DESC, submitted_at ASC LIMIT -1 OFFSET ?3`)
              .bind(b.season, b.players, KEEP_N).all();
      for (const row of rows.results || []) {
        if (!row.blob_key) continue;  // already score-only
        try { await env.REPLAYS.delete(row.blob_key); } catch (e) {}
        await env.DB.prepare(
            `UPDATE scores SET blob_key = ''
             WHERE season = ?1 AND run_id = ?2`)
            .bind(b.season, row.run_id).run();
        demoted++;
      }
    }
    if (demoted) console.log(`retention: demoted ${demoted} row(s) to score-only`);
  },
};

async function within_limit(env, ip, action) {
  const limiter = env.LIMITS.get(env.LIMITS.idFromName(ip));
  try {
    const resp = await limiter.fetch(`https://limiter/hit?action=${action}`);
    const data = await resp.json();
    return !!data.allowed;
  } catch (e) {
    return true; // a limiter hiccup must not lock everyone out
  }
}

function reject_ws(reason) {
  const pair = new WebSocketPair();
  pair[1].accept();
  pair[1].send(JSON.stringify({ t: "err", reason }));
  pair[1].close(1000);
  return new Response(null, { status: 101, webSocket: pair[0] });
}

// ---- per-IP rate limiter (same persistence discipline as signal's) ------

export class Limiter {
  constructor(state, env) {
    this.state = state;
    this.env = env;
    this.l = {}; // action -> {start, count}
    state.blockConcurrencyWhile(async () => {
      const stored = await state.storage.get("limits");
      if (stored) this.l = stored;
    });
  }

  async fetch(request) {
    const action = new URL(request.url).searchParams.get("action");
    const cfg = limit_for(this.env, action);
    const now = Date.now();
    let a = this.l[action];
    if (!a || now - a.start > cfg.window_ms) a = { start: now, count: 0 };
    a.count++;
    this.l[action] = a;
    const allowed = a.count <= cfg.limit;
    await this.state.storage.put("limits", this.l);
    // Self-clean once every window has lapsed (signal Limiter's rule).
    const longest = Math.max(...Object.values(LIMITS).map((c) => c.window_ms));
    await this.state.storage.setAlarm(now + longest + 1000);
    return new Response(JSON.stringify({ allowed }),
                        { headers: { "Content-Type": "application/json" } });
  }

  async alarm() {
    const now = Date.now();
    const live = Object.entries(this.l).some(([action, a]) =>
        now - a.start <= (LIMITS[action] || LIMITS.conn).window_ms);
    if (!live) await this.state.storage.deleteAll();
  }
}

// ---- per-connection session DO ------------------------------------------

export class Session {
  constructor(state, env) {
    this.state = state;
    this.env = env;
    this.ip = "local";
    this.queries = 0;
    this.submits = 0;
    this.fetches = 0;
    // In-flight upload: null, or {size, identity, chunks:[], received}.
    this.upload = null;
    this.idle_timer = null;
  }

  async fetch(request) {
    const url = new URL(request.url);
    if (url.pathname !== "/connect")
      return new Response("not found", { status: 404 });
    this.ip = url.searchParams.get("ip") || "local";
    const pair = new WebSocketPair();
    this.state.acceptWebSocket(pair[1]);
    this.arm_idle(pair[1]);
    return new Response(null, { status: 101, webSocket: pair[0] });
  }

  arm_idle(ws) {
    if (this.idle_timer) clearTimeout(this.idle_timer);
    this.idle_timer = setTimeout(() => {
      try { ws.close(1000, "idle"); } catch (e) {}
    }, CONN_IDLE_MS);
  }

  send(ws, obj) { try { ws.send(JSON.stringify(obj)); } catch (e) {} }
  err(ws, reason) { this.send(ws, { t: "err", reason }); }
  fail(ws, reason) { this.err(ws, reason); try { ws.close(1000); } catch (e) {} }

  async webSocketMessage(ws, message) {
    this.arm_idle(ws);
    try {
      if (typeof message === "string") await this.on_json(ws, message);
      else await this.on_chunk(ws, message);
    } catch (e) {
      // A thrown handler (D1 hiccup, R2 error) must not strand the client
      // in silence — it is waiting on a reply frame.
      console.log(`session error: ${e && e.message}`);
      this.fail(ws, "internal");
    }
  }

  async webSocketClose() { if (this.idle_timer) clearTimeout(this.idle_timer); }
  async webSocketError() { if (this.idle_timer) clearTimeout(this.idle_timer); }

  async on_json(ws, text) {
    if (text.length > 32 * 1024) return this.fail(ws, "bad-frame");
    let msg;
    try { msg = JSON.parse(text); } catch (e) { return this.fail(ws, "bad-frame"); }
    if (!msg || typeof msg !== "object") return this.fail(ws, "bad-frame");

    if (msg.t === "seasons") {
      // Season browser (LEADERBOARD.md): the seasons that exist, newest
      // submission first, across BOTH boards (the client's SOLO/CO-OP
      // toggle works within a season). Takes no season argument, so it
      // sits before the season-keyed block below; budgeted as a query.
      if (++this.queries > CONN_MAX_QUERIES) return this.fail(ws, "rate-limited");
      if (!(await within_limit(this.env, this.ip, "query")))
        return this.err(ws, "rate-limited");
      await ensure_schema(this.env.DB);
      const rows = await this.env.DB.prepare(
          `SELECT season, MAX(submitted_at) AS newest, COUNT(*) AS n
           FROM scores GROUP BY season ORDER BY newest DESC LIMIT 50`).all();
      this.send(ws, {
        t: "seasons",
        rows: (rows.results || []).map((r) => ({
          season: r.season, newest: Number(r.newest), count: Number(r.n),
        })),
      });
      return;
    }

    if (msg.t === "qualify" || msg.t === "rank-of" || msg.t === "top") {
      if (++this.queries > CONN_MAX_QUERIES) return this.fail(ws, "rate-limited");
      // Per-IP aggregate read budget (see LIMITS): reads are cheap to retry,
      // so a refusal is a non-fatal err, not a socket close.
      if (!(await within_limit(this.env, this.ip, "query")))
        return this.err(ws, "rate-limited");
      const season = typeof msg.season === "string" ? msg.season : "";
      const players = msg.players === 2 ? 2 : 1;
      if (!season_ok(season)) return this.err(ws, "bad-season");
      await ensure_schema(this.env.DB);
      if (msg.t === "top") {
        const count = Math.min(Math.max(Number(msg.count) || 25, 1), KEEP_N);
        const rows = await this.env.DB.prepare(
            `SELECT run_id, score, generation, duration_ms, submitted_at,
                    name, platform, verified, blob_key, format, save_format
             FROM scores WHERE season = ?1 AND players = ?2
             ORDER BY score DESC, submitted_at ASC LIMIT ?3`)
            .bind(season, players, count).all();
        this.send(ws, {
          t: "top",
          rows: (rows.results || []).map((r, i) => ({
            rank: i + 1, name: r.name, platform: Number(r.platform),
            verified: !!Number(r.verified), score: Number(r.score),
            generation: Number(r.generation),
            duration_ms: Number(r.duration_ms),
            date: Number(r.submitted_at),
            has_replay: r.blob_key !== "", run_id: r.run_id,
            // The replay's format + embedded savegame version, so a client
            // browsing an old season can grey out rows its build cannot
            // play back BEFORE downloading (0 = row predates the columns).
            format: Number(r.format), save_format: Number(r.save_format),
          })),
        });
        return;
      }
      const score = Number(msg.score) >>> 0;
      // Projected rank of a NOT-yet-submitted run: it would sort AFTER
      // existing equal scores (top/retention order by score DESC,
      // submitted_at ASC), so ties count as ABOVE it — consistent with
      // where the run would actually chart. (The post-insert `placed`
      // rank uses rank_of, which counts strictly-better rows.) `players`
      // is echoed so a client that flips SOLO/CO-OP mid-flight can drop a
      // stale answer.
      const place = await projected_rank(this.env.DB, season, players, score);
      if (msg.t === "rank-of") {
        this.send(ws, { t: "rank-of", place, players });
        return;
      }
      const cut = await cutline(this.env.DB, season, players);
      this.send(ws, { t: "qualify", place, players, cutline: cut,
                      would_place: place <= KEEP_N });
      return;
    }

    if (msg.t === "submit") {
      if (this.upload) return this.fail(ws, "bad-frame");
      if (++this.submits > CONN_MAX_SUBMITS) return this.fail(ws, "rate-limited");
      const size = Number(msg.size) >>> 0;
      if (size < MIN_SUBMISSION_BYTES || size > MAX_SUBMISSION_BYTES)
        return this.err(ws, "too-large");
      // A refusal, not a close (like the query budget above): closing
      // here made the client render the whole screen as LEADERBOARD
      // UNAVAILABLE instead of the upload row's TRY LATER (field report
      // — the Closed teardown outranked the refusal it arrived with).
      // The per-connection CONN_MAX_SUBMITS cap above still closes: that
      // one is a misbehaving-client guard, not a budget answer.
      if (!(await within_limit(this.env, this.ip, "submit")))
        return this.err(ws, "rate-limited");
      // Attestation is an admission requirement (LEADERBOARD.md): verify
      // BEFORE accepting megabytes of chunks — a spoofed submit costs the
      // spoofer the round-trip, not us the bandwidth.
      const identity = await verify_identity(
          this.env, Number(msg.platform) >>> 0, msg.name, msg.cred);
      if (!identity) {
        console.log(`submit refused: unverified platform=${msg.platform}`);
        return this.err(ws, "unverified");
      }
      // DEV/TEST ONLY: force the FIRST submit on a connection to look
      // unverified, so the client's warm-a-fresh-credential-and-retry path
      // (credential-lifecycle hardening) can be exercised without a real
      // single-use collision. The retry (2nd submit, same socket) passes.
      // Never set in production.
      if (this.env.REJECT_FIRST_VERIFY && !this.forced_reject_once_) {
        this.forced_reject_once_ = true;
        console.log("submit refused: REJECT_FIRST_VERIFY (dev retry test)");
        return this.err(ws, "unverified");
      }
      this.upload = { size, identity, chunks: [], received: 0 };
      this.send(ws, { t: "submit-ok" });
      return;
    }

    if (msg.t === "submit-end") {
      const up = this.upload;
      this.upload = null;
      if (!up) return this.fail(ws, "bad-frame");
      if (up.received !== up.size) return this.err(ws, "size-mismatch");
      const blob = new Uint8Array(up.size);
      let off = 0;
      for (const c of up.chunks) { blob.set(c, off); off += c.length; }
      await this.finish_submit(ws, blob, up.identity);
      return;
    }

    if (msg.t === "fetch") {
      if (++this.fetches > CONN_MAX_FETCHES) return this.fail(ws, "rate-limited");
      const season = typeof msg.season === "string" ? msg.season : "";
      const run_id = typeof msg.run_id === "string" ? msg.run_id : "";
      if (!season_ok(season) || !/^[0-9]{1,20}$/.test(run_id))
        return this.err(ws, "no-replay");
      await ensure_schema(this.env.DB);
      const row = await this.env.DB.prepare(
          `SELECT blob_key FROM scores WHERE season = ?1 AND run_id = ?2`)
          .bind(season, run_id).first();
      if (!row || row.blob_key === "") return this.err(ws, "no-replay");
      const obj = await this.env.REPLAYS.get(row.blob_key);
      if (!obj) return this.err(ws, "no-replay"); // row/blob drifted (cron race)
      const bytes = new Uint8Array(await obj.arrayBuffer());
      this.send(ws, { t: "fetch-ok", size: bytes.length });
      for (let p = 0; p < bytes.length; p += CHUNK) {
        try { ws.send(bytes.subarray(p, Math.min(p + CHUNK, bytes.length))); }
        catch (e) { return; }
      }
      this.send(ws, { t: "fetch-end" });
      return;
    }

    this.fail(ws, "bad-frame");
  }

  async on_chunk(ws, message) {
    const up = this.upload;
    if (!up) return this.fail(ws, "bad-frame");
    const chunk = new Uint8Array(message);
    if (up.received + chunk.length > up.size) {
      this.upload = null;
      return this.fail(ws, "size-mismatch");
    }
    // Cap chunk COUNT as well as bytes: a tiny-frame flood otherwise pins
    // memory (millions of typed-array views) far beyond the byte cap.
    if (up.chunks.length >= MAX_UPLOAD_CHUNKS) {
      this.upload = null;
      return this.fail(ws, "too-many-chunks");
    }
    up.chunks.push(chunk);
    up.received += chunk.length;
  }

  async finish_submit(ws, blob, identity) {
    const v = validate_submission(blob);
    if (!v.ok) {
      console.log(`submit refused: ${v.reason}`);
      return this.err(ws, v.reason);
    }
    const hd = v.header;
    const db = this.env.DB;
    await ensure_schema(db);
    const players = hd.player_count;
    const key = await platform_key(identity.account);

    // Same run resubmitted (clean-abandon uploaded, then resumed and
    // improved): upsert only a strictly better score.
    const run_row = await db.prepare(
        `SELECT score FROM scores WHERE season = ?1 AND run_id = ?2`)
        .bind(hd.season, hd.run_id).first();
    if (run_row && Number(run_row.score) >= hd.score)
      return this.err(ws, "already-submitted");

    // One row per player per season+board (fast-path refusal — the
    // atomic upsert below is the real guarantee): if the player already
    // has a row that is not worse, don't even store the blob.
    const mine = await db.prepare(
        `SELECT run_id, score, blob_key FROM scores
         WHERE season = ?1 AND players = ?2 AND platform_key = ?3
           AND run_id != ?4`)
        .bind(hd.season, players, key, hd.run_id).first();
    if (mine && Number(mine.score) >= hd.score)
      return this.err(ws, "not-best");

    const blob_key = blob_key_for(hd.season, hd.run_id);
    await this.env.REPLAYS.put(blob_key, blob);
    // Atomic supersede: ON CONFLICT on the UNIQUE (season, players,
    // platform_key) index updates the player's existing row to this run
    // in one statement, but ONLY when this score is strictly better —
    // so two concurrent same-account submits can't lose the higher one,
    // and no interleaving leaves duplicate rows. A worse score racing in
    // no-ops here (its blob is cleaned up below).
    await db.prepare(
        `INSERT INTO scores(season, players, run_id, score,
           generation, duration_ms, submitted_at, name, platform, verified,
           platform_key, blob_key, format, save_format)
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14)
         ON CONFLICT(season, players, platform_key) DO UPDATE SET
           run_id = excluded.run_id, score = excluded.score,
           generation = excluded.generation, duration_ms = excluded.duration_ms,
           submitted_at = excluded.submitted_at, name = excluded.name,
           platform = excluded.platform, verified = excluded.verified,
           blob_key = excluded.blob_key, format = excluded.format,
           save_format = excluded.save_format
         WHERE excluded.score > scores.score`)
        .bind(hd.season, players, hd.run_id, hd.score, hd.generation,
              hd.duration_ms, Date.now(), identity.name, identity.platform,
              identity.verified ? 1 : 0, key, blob_key,
              hd.format_version, hd.save_version).run();
    // Did this run win the slot? The surviving row for this player tells
    // us unambiguously (covers the lost-the-WHERE race too).
    const survivor = await db.prepare(
        `SELECT run_id FROM scores
         WHERE season = ?1 AND players = ?2 AND platform_key = ?3`)
        .bind(hd.season, players, key).first();
    const won = survivor && survivor.run_id === hd.run_id;
    if (won) {
      // The superseded personal best's blob is now orphaned (its row was
      // replaced by the upsert) — delete it.
      if (mine && mine.blob_key && mine.blob_key !== blob_key)
        try { await this.env.REPLAYS.delete(mine.blob_key); } catch (e) {}
    } else {
      // A concurrent better submission won; our blob is orphaned.
      try { await this.env.REPLAYS.delete(blob_key); } catch (e) {}
      return this.err(ws, "not-best");
    }
    const rank = await rank_of(db, hd.season, players, hd.score);
    console.log(`placed: season=${hd.season} players=${players} ` +
                `score=${hd.score} rank=${rank} platform=${identity.platform}`);
    this.send(ws, { t: "placed", rank });
  }
}
