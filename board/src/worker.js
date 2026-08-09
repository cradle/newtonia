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
//   <- {t:"top", season, players,     (echoed: stale-answer drop, like
//       rows:[{rank,name,platform,     qualify/rank-of)
//              verified,score,generation,duration_ms,date,has_replay,
//              run_id,format,save_format}]}
//   -> {t:"seasons"}
//   <- {t:"seasons", rows:[{season,newest,count}]}   canonical (s<N>)
//                                          seasons only, newest-first, max 50
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
// Read-only HTTP endpoints for the WEBSITE (the GitHub Pages /leaderboard/
// page and the /play/?replay= watch deep link — LEADERBOARD.md "site
// leaderboard"). Both are public data the WS protocol already serves to any
// native client, so they answer with Access-Control-Allow-Origin: * and
// change nothing about admission (submissions stay WS + attestation only):
//   GET /site/leaderboard.json          the snapshot (top KEEP_N of every
//                                       canonical season, both boards),
//                                       republished by the daily retention
//                                       cron and rebuilt on read whenever
//                                       a newer submission exists
//   GET /replay/<season>/<run_id>.nrp   a charting row's replay blob
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
  shape_note,
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
// Idle sockets are closed after this long. NOTE the side effect: a pending
// setTimeout keeps the DO resident, so this timer is also the reason
// hibernation never actually happens today. The budgets no longer DEPEND on
// that (Session.hydrate carries them on the socket — S5), so converting this
// to the storage alarm the hibernation docs recommend is now a safe change
// to make for its own reasons.
const CONN_IDLE_MS = 10 * 60 * 1000;

// Per-IP fixed windows (Limiter DO). Connects gate new sockets; reads and
// fetches are limited in aggregate so connection churn can't multiply
// D1/R2 cost past the free tier (query worst case: 200 × 100 rows =
// 20k rows / 10 min / IP ≈ 2.9M rows/day, under the 5M/day D1 free ceiling
// even before other traffic); submits stay the strict one. NOTE: like the
// signal worker this keys on the client IP (IPv6 collapsed to /64), so a
// large IPv6 allocation can still spread load — an accepted limitation
// shared with signal, not a per-IP-defeatable gap.
// SUBMIT_LIMIT and CONN_LIMIT are dev/test vars (wrangler dev
// --var SUBMIT_LIMIT:250 --var CONN_LIMIT:500) so the protocol test's
// burst of submissions/connections from one IP doesn't trip the
// production windows; never set in production — and since S6 they cannot
// take effect there, since limit_for ignores them unless the request
// arrived on a loopback host (is_dev_host). The full-board qualify test
// needs both: 100 fill rows at 2 submits per socket is 50 extra
// connections.
const LIMITS = {
  conn: { window_ms: 10 * 60 * 1000, limit: 60 },
  query: { window_ms: 10 * 60 * 1000, limit: 200 },
  fetch: { window_ms: 60 * 60 * 1000, limit: 40 },
  submit: { window_ms: 60 * 60 * 1000, limit: 6 },
};

function limit_for(env, action, dev) {
  const cfg = LIMITS[action] || LIMITS.conn;
  if (!dev) return cfg;  // the widened windows are dev-only (see is_dev_host)
  if (action === "submit" && env && Number(env.SUBMIT_LIMIT) > 0)
    return { ...cfg, limit: Number(env.SUBMIT_LIMIT) };
  if (action === "conn" && env && Number(env.CONN_LIMIT) > 0)
    return { ...cfg, limit: Number(env.CONN_LIMIT) };
  return cfg;
}

// Is this request talking to a LOCAL dev worker? Every dev/test switch below
// (FAKE_VERIFY, REJECT_FIRST_VERIFY, the widened rate windows) requires this
// as a SECOND condition, so none of them can be turned on by configuration
// alone (LEADERBOARD.md S6). FAKE_VERIFY in particular attests any claim —
// one stray dashboard variable would otherwise convert production into an
// open board with no attestation at all, silently.
//
// Every harness runs `wrangler dev --local` and connects over loopback
// (board_test/whitelist_test on 127.0.0.1, test/e2e/leaderboard.sh on
// 127.0.0.1:8799), while a deployed worker is only ever reached at its
// workers.dev or custom hostname — Cloudflare routes by that hostname, so a
// forged Host header cannot arrive here wearing a loopback name. A remote
// `wrangler dev` preview is deliberately NOT dev by this test: something on
// the public edge should not be faking attestation.
export function is_dev_host(hostname) {
  return hostname === "localhost" || hostname === "127.0.0.1" ||
         hostname === "::1" || hostname === "[::1]" ||
         hostname.endsWith(".localhost");
}

// Client-controlled text that reaches a log line. Strips anything outside
// printable ASCII (a newline lets a hostile value forge whole log entries)
// and caps the length — the same boundary net_board_sanitize enforces on the
// game side, in the other direction.
export function log_str(v, max = 64) {
  let out = "";
  for (const c of String(v)) {
    if (out.length >= max) break;
    const b = c.charCodeAt(0);
    if (b >= 0x20 && b < 0x7f) out += c;
  }
  return out;
}

// v1 has no web client (LEADERBOARD.md: no leaderboard on web in the first
// release), so browser origins are refused except local dev. Native
// clients send no Origin and pass. ALLOWED_ORIGINS secret overrides
// without a redeploy (comma-separated; leading dot = subdomain match).
// This gates the WS endpoint only — the website's read-only views go
// through the plain-HTTP /site + /replay endpoints above, which serve
// public data to any origin and cannot submit.
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

// Canonical season key: the deliberate SEASON-file stamps (s1, s2, ...).
// The browser lists only these everywhere; PRODUCTION also refuses
// ADMISSION of anything else (decided with Glenn 2026-08-01: prod is a
// whitelist, beta stays open for testing). The gate is the top-level
// wrangler.toml var CANONICAL_SEASONS_ONLY="1" — production config is the
// default truth; the beta env sets no vars, and the test harnesses pass
// --var CANONICAL_SEASONS_ONLY:0 alongside FAKE_VERIFY as their explicit
// divergence from production.
function season_canonical(season) { return /^s[0-9]+$/.test(season); }
function canonical_only(env) {
  return !!env && env.CANONICAL_SEASONS_ONLY === "1";
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

export async function verify_identity(env, platform, name, cred, dev) {
  const claimed = strip_name(name);
  if (typeof cred !== "string" || !cred || cred.length > MAX_CRED) return null;
  if (dev && env.FAKE_VERIFY === "1") {
    // Dev/e2e shortcut (wrangler dev only): attest the claim without a
    // platform backend — but still REQUIRE a non-empty credential (above),
    // like every real backend, so the client's empty-at-submit case tests
    // true. The cred is logged so the retry e2e can assert the resubmit
    // carried a genuinely different one. The account derives from the name
    // so two test "players" stay distinct. NEVER set in production — and
    // now it cannot BE set in production: `dev` demands the request arrived
    // over loopback (is_dev_host), so the variable alone does nothing on a
    // deployed worker. Compared to "1" exactly (the value every harness
    // passes): a plain truthiness test made FAKE_VERIFY=0 ENABLE the fake.
    console.log(`fake-verify: cred=${log_str(cred)}`);
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
    // The site snapshot's freshness probe (serve_snapshot) is
    // MAX(submitted_at) over the whole table on every page view — this
    // index makes that a head lookup instead of a full scan (D1 bills
    // rows scanned).
    db.prepare(`CREATE INDEX IF NOT EXISTS scores_submitted
                ON scores(submitted_at DESC)`),
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

// Board slot for a player count: every co-op run (>= 2 players) competes
// on the single players=2 co-op board — 3P/4P get no boards of their own
// (FOURPLAYER.md D10). Applied authoritatively on submit AND on every
// query that carries a `players` field, so the `scores.players` column
// only ever holds 1 or 2 regardless of what a client sends.
function board_slot(players) {
  return players >= 2 ? 2 : 1;
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

// ---- site endpoints ------------------------------------------------------
// The website's read path (the Pages /leaderboard/ page and the
// /play/?replay= watch link). Everything here is data the WS protocol
// already hands to any native client; the only thing the site can NOT do is
// submit, which stays WS + platform attestation.

// R2 key of the daily snapshot. Lives in the replay bucket under a
// reserved prefix the orphan sweep skips (sweep_orphans) — every other
// object in the bucket is a blob_key some row points at.
const SNAPSHOT_KEY = "site/leaderboard.json";

const CORS = { "Access-Control-Allow-Origin": "*" };

function site_error(status, text) {
  return new Response(text, { status, headers: CORS });
}

// One board's rows, in the exact shape the WS `top` reply uses — the page
// and the game render the same fields.
function site_rows(results) {
  return (results || []).map((r, i) => ({
    rank: i + 1, name: r.name, platform: Number(r.platform),
    verified: !!Number(r.verified), score: Number(r.score),
    generation: Number(r.generation), duration_ms: Number(r.duration_ms),
    date: Number(r.submitted_at),
    has_replay: r.blob_key !== "", run_id: r.run_id,
    format: Number(r.format), save_format: Number(r.save_format),
  }));
}

// The whole site snapshot: top KEEP_N of BOTH boards for every canonical
// season (the WS seasons listing's filter), newest-first, with the live
// season per players count flagged. Bounded work: <= 50 seasons x 2 boards
// x KEEP_N rows, run once a day by the cron (and once on a cold miss).
export async function build_site_snapshot(env) {
  await ensure_schema(env.DB);
  const seasons = await env.DB.prepare(
      `SELECT season, MAX(submitted_at) AS newest, COUNT(*) AS n
       FROM scores GROUP BY season ORDER BY newest DESC LIMIT 200`).all();
  const canonical = (seasons.results || [])
      .filter((r) => season_canonical(r.season)).slice(0, 50);
  const boards = [];
  for (const players of [1, 2]) {
    for (const s of canonical) {
      const rows = await env.DB.prepare(
          `SELECT run_id, score, generation, duration_ms, submitted_at,
                  name, platform, verified, blob_key, format, save_format
           FROM scores WHERE season = ?1 AND players = ?2
           ORDER BY score DESC, submitted_at ASC LIMIT ?3`)
          .bind(s.season, players, KEEP_N).all();
      const list = site_rows(rows.results);
      if (!list.length) continue; // a season can be solo- or co-op-only
      boards.push({ season: s.season, players, live: false, rows: list });
    }
    // The live board per players count = the listed board with the newest
    // submission — computed over the LISTED (canonical) boards, not the
    // whole table, so a dev-season submission on beta can't leave the site
    // with no live board to open on.
    let newest = null;
    for (const b of boards) {
      if (b.players !== players) continue;
      b.newest_ = Math.max(...b.rows.map((r) => r.date));
      if (!newest || b.newest_ > newest.newest_) newest = b;
    }
    for (const b of boards)
      if (b.players === players) { b.live = b === newest; delete b.newest_; }
  }
  return { generated_at: Date.now(), boards };
}

// Store the snapshot with its build time in R2 metadata, so the freshness
// probe below can read it without parsing the (potentially large) body.
async function store_snapshot(env, snap) {
  await env.REPLAYS.put(SNAPSHOT_KEY, JSON.stringify(snap), {
    customMetadata: { generated_at: String(snap.generated_at) },
  });
}

// A rebuild is ~10,000x the cost of an ordinary view (the GROUP BY scan
// plus up to 50 seasons x 2 boards x KEEP_N rows, plus an R2 write), and it
// is reachable from an unauthenticated GET — so it must not be per-request
// work (security review 2026-08-04, F1). Two bounds, deliberately chosen so
// that a SUCCESSFUL rebuild is never delayed (a new score must show on the
// next view — that freshness is the whole point of the probe, field-
// verified 2026-08-04):
//   - SINGLE-FLIGHT: concurrent views share one rebuild instead of each
//     running their own, so the flood of views inside one stale window
//     costs one rebuild per isolate, not one per request. The window
//     closes as soon as the rebuild stores, so the steady-state ceiling is
//     one rebuild per SUBMISSION — and submissions are already the most
//     tightly rate-limited action there is (6/hour/IP).
//   - FAILURE BACKOFF: only failures cool down. Discarding the stale body
//     before attempting a rebuild meant any persistent failure (a D1
//     hiccup, an R2 write error, the CPU cap on a large board) returned
//     500 AND left the snapshot stale, so every later view retried the
//     rebuild forever — a self-sustaining loop that needed no attacker.
//     Now a failure serves the stale body and stops retrying for a minute.
const REBUILD_BACKOFF_MS = 60 * 1000;
let rebuild_inflight = null;   // per-isolate; null when none is running
let rebuild_blocked_until = 0; // set only after a failed rebuild

function rebuild_snapshot(env) {
  if (rebuild_inflight) return rebuild_inflight;
  const done = (async () => {
    try {
      const snap = await build_site_snapshot(env);
      await store_snapshot(env, snap);
      return snap;
    } catch (e) {
      rebuild_blocked_until = Date.now() + REBUILD_BACKOFF_MS;
      throw e;
    }
  })().finally(() => { rebuild_inflight = null; });
  rebuild_inflight = done;
  return done;
}

// Has a submission landed since this snapshot was built? One indexed
// head-read (scores_submitted makes it a seek, not a scan). A snapshot with
// no build time is a pre-metadata object from an older deploy: treat it as
// stale once — store_snapshot always writes the field, so it self-heals.
async function snapshot_stale(env, built) {
  if (!built) return true;
  await ensure_schema(env.DB);
  const r = await env.DB.prepare(
      `SELECT MAX(submitted_at) AS newest FROM scores`).first();
  return !!(r && Number(r.newest) > built);
}

async function serve_snapshot(request, env, dev) {
  const ip = rate_key(request.headers.get("CF-Connecting-IP") || "local");
  if (!(await within_limit(env, ip, "query", dev)))
    return site_error(429, "rate limited");
  const obj = await env.REPLAYS.get(SNAPSHOT_KEY);
  let body = obj ? await obj.text() : null;
  const built = obj ? Number((obj.customMetadata || {}).generated_at) || 0 : 0;
  // `!body` short-circuits the probe, so a cold miss costs no D1 read.
  if (!body || await snapshot_stale(env, built)) {
    if (body && Date.now() < rebuild_blocked_until) {
      // A recent rebuild failed. Serve what we have rather than hammering
      // the failing path once per view.
    } else {
      try {
        body = JSON.stringify(await rebuild_snapshot(env));
      } catch (e) {
        console.log(`site: snapshot rebuild failed: ${log_str(e && e.message)}`);
        // Stale beats nothing; with no stored body there is nothing to serve.
        if (!body) return site_error(503, "leaderboard unavailable");
      }
    }
  }
  return new Response(body, { headers: {
    ...CORS,
    "Content-Type": "application/json",
    // Short browser cache: new scores should appear within minutes, and
    // the freshness probe already keeps repeat worker hits cheap.
    "Cache-Control": "public, max-age=300",
  } });
}

// The blob download the WS `fetch` flow serves, as a plain GET for the web
// client's watch deep link. Same admission checks as the WS path: the row
// must exist and still hold its blob (charting rank).
async function serve_replay(request, env, dev, season, run_id) {
  const ip = rate_key(request.headers.get("CF-Connecting-IP") || "local");
  if (!season_ok(season) || !/^[0-9]{1,20}$/.test(run_id))
    return site_error(404, "no replay");
  if (!(await within_limit(env, ip, "fetch", dev)))
    return site_error(429, "rate limited");
  await ensure_schema(env.DB);
  const row = await env.DB.prepare(
      `SELECT blob_key FROM scores WHERE season = ?1 AND run_id = ?2`)
      .bind(season, run_id).first();
  if (!row || row.blob_key === "") return site_error(404, "no replay");
  const obj = await env.REPLAYS.get(row.blob_key);
  if (!obj) return site_error(404, "no replay"); // row/blob drift (cron race)
  return new Response(obj.body, { headers: {
    ...CORS,
    "Content-Type": "application/octet-stream",
    // A run_id's blob can be replaced by the same account improving the
    // same run, so cap staleness at an hour rather than caching forever.
    "Cache-Control": "public, max-age=3600",
  } });
}

// ---- worker entry --------------------------------------------------------

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    // Website read endpoints (plain HTTP, CORS *) — see the header comment.
    // The kill switch covers them like the WS path.
    if (url.pathname === "/site/leaderboard.json" ||
        url.pathname.startsWith("/replay/")) {
      if (request.method !== "GET") return site_error(405, "method not allowed");
      if (env.DISABLED) return site_error(503, "disabled");
      const dev = is_dev_host(url.hostname);
      if (url.pathname === "/site/leaderboard.json")
        return serve_snapshot(request, env, dev);
      const m = url.pathname.match(/^\/replay\/([^/]+)\/([^/]+)\.nrp$/);
      if (!m) return site_error(404, "no replay");
      let season;
      try { season = decodeURIComponent(m[1]); } catch (e) {
        return site_error(404, "no replay");
      }
      return serve_replay(request, env, dev, season, m[2]);
    }
    if (url.pathname !== "/board")
      return new Response("newtonia-board", { status: 200 });
    if (request.headers.get("Upgrade") !== "websocket")
      return new Response("expected websocket", { status: 426 });
    if (!origin_allowed(env, request.headers.get("Origin")))
      return reject_ws("forbidden-origin");
    // Kill switch, same shape as the signal worker's.
    if (env.DISABLED) return reject_ws("disabled");

    const dev = is_dev_host(url.hostname);
    const ip = rate_key(request.headers.get("CF-Connecting-IP") || "local");
    if (!(await within_limit(env, ip, "conn", dev)))
      return reject_ws("rate-limited");

    // One DO per connection: isolates state, and DO message events get
    // their own CPU allotment (a 32 MB reassembly would flirt with the
    // free plan's per-invocation budget in a stateless worker socket).
    const session = env.SESSIONS.get(env.SESSIONS.newUniqueId());
    return session.fetch(new Request(
        `https://session/connect?ip=${encodeURIComponent(ip)}` +
        (dev ? "&dev=1" : ""), request));
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
    // Best-effort: the demote pass above has already committed its work, and
    // an R2 hiccup in the sweep must not fail the whole cron invocation (or
    // hide the demote count behind an exception). The next run picks up
    // whatever this one missed.
    try {
      await sweep_orphans(env);
    } catch (e) {
      console.log(`retention: orphan sweep failed: ${log_str(e && e.message)}`);
    }
    // Publish the website's daily leaderboard snapshot (LEADERBOARD.md
    // "site leaderboard") AFTER retention, so it never lists a blob the
    // demote pass just deleted. Best-effort like the sweep: a failed
    // publish leaves yesterday's snapshot serving, which is exactly the
    // staleness the site already tolerates.
    // Goes through rebuild_snapshot (not build+store directly) so it
    // shares the single-flight guard with any concurrent view and arms the
    // min-interval window behind it.
    try {
      const snap = await rebuild_snapshot(env);
      console.log(`site: published snapshot (${snap.boards.length} board(s))`);
    } catch (e) {
      console.log(`site: snapshot publish failed: ${log_str(e && e.message)}`);
    }
  },
};

// Objects in R2 that no row points at (S2). The submit path deletes its own
// blob on every failure now, so this should find nothing — which is exactly
// why it exists: an orphan is invisible (no row mentions it), costs storage
// forever, and the retention pass above can never see one because it walks
// ROWS. Anything a future code path leaks lands here within a day.
//
// The grace period is not politeness, it is correctness: a submission in
// flight has already stored its blob and not yet inserted its row, and
// deleting that would break a live upload. Only objects older than a full
// day are candidates.
const ORPHAN_GRACE_MS = 24 * 60 * 60 * 1000;

async function sweep_orphans(env) {
  // One pass over the blob_keys D1 knows: bounded by the row count (KEEP_N
  // per board per season), so this is far cheaper than a query per object.
  const known = new Set();
  const rows = await env.DB.prepare(
      `SELECT blob_key FROM scores WHERE blob_key != ''`).all();
  for (const r of rows.results || []) known.add(r.blob_key);

  const cutoff = Date.now() - ORPHAN_GRACE_MS;
  let cursor;
  let swept = 0;
  do {
    const page = await env.REPLAYS.list({ cursor, limit: 1000 });
    for (const obj of page.objects || []) {
      // The snapshot is not a replay — no row ever points at it, so without
      // this skip the sweep would delete it a day after every publish.
      // Matched EXACTLY, not by prefix: `site` is a legal season key
      // (season_ok allows it), so blob_key_for can put a submitted replay
      // at site/<run_id>.nrp — a prefix skip would exempt that whole
      // namespace from the orphan backstop, letting anything leaked there
      // grow R2 forever, invisible to every reaper (security review
      // 2026-08-04, F2; confirmed reachable on an env with no season
      // whitelist).
      if (obj.key === SNAPSHOT_KEY) continue;
      if (known.has(obj.key)) continue;
      const uploaded = obj.uploaded ? new Date(obj.uploaded).getTime() : 0;
      if (!(uploaded && uploaded < cutoff)) continue;  // in flight, or unknown
      try { await env.REPLAYS.delete(obj.key); swept++; } catch (e) {}
    }
    cursor = page.truncated ? page.cursor : null;
  } while (cursor);
  if (swept) console.log(`retention: swept ${swept} orphaned blob(s)`);
}

// Actions where an UNANSWERABLE limiter means "no", not "yes" (S5). The
// per-IP Limiter is the only durable bound on submissions — the
// per-connection cap resets on reconnect by design — so failing open on it
// means an outage lifts the submit ceiling entirely, and a submit is the
// expensive, abuse-sensitive path (an R2 write plus D1 rows). Reads stay
// fail-open: they are cheap to serve and locking the whole board out over a
// limiter hiccup is the worse failure. A refused submit is not lost work
// either — the client's upload row offers TRY LATER, and best.nrp keeps the
// run until it succeeds.
const FAIL_CLOSED = ["submit"];

export async function within_limit(env, ip, action, dev) {
  const limiter = env.LIMITS.get(env.LIMITS.idFromName(ip));
  try {
    const resp = await limiter.fetch(
        `https://limiter/hit?action=${action}` + (dev ? "&dev=1" : ""));
    const data = await resp.json();
    return !!data.allowed;
  } catch (e) {
    const open = !FAIL_CLOSED.includes(action);
    console.log(`limiter unavailable for ${log_str(action, 16)}: ` +
                `${open ? "allowing" : "refusing"} (${log_str(e && e.message)})`);
    return open;
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
    const params = new URL(request.url).searchParams;
    const action = params.get("action");
    const cfg = limit_for(this.env, action, params.get("dev") === "1");
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
    // Set from the connect URL: true only when the request arrived on a
    // loopback host, which is what every dev/test switch also requires.
    this.dev = false;
    this.queries = 0;
    this.submits = 0;
    this.fetches = 0;
    // In-flight upload: null, or {size, identity, chunks:[], received}.
    this.upload = null;
    this.idle_timer = null;
    // Have we restored this connection's counters from the socket yet? See
    // hydrate() — false on a freshly constructed instance, which is exactly
    // what a post-hibernation delivery gets.
    this.hydrated = false;
  }

  // Per-connection state lives in memory, and `acceptWebSocket` opts this DO
  // into hibernation — so an eviction between messages would reconstruct the
  // class with every budget back at zero, and `ip`/`dev` back at their
  // defaults (S5). Today the idle setTimeout keeps the object resident, so
  // it does not happen; that is an accident of an unrelated timer, not a
  // guarantee, and it would quietly stop being true the day the timer
  // becomes the storage alarm the hibernation docs recommend.
  //
  // So carry the state on the SOCKET, which is what survives hibernation by
  // design: restore once per instance, persist whenever a budget moves. The
  // in-memory object stays authoritative after the first hydrate, so
  // interleaved handlers (a message delivered while another awaits a verify)
  // still share one set of counters — no read-modify-write race.
  //
  // The in-flight upload deliberately does NOT ride along: it is up to 32 MB
  // of chunks, far past what an attachment may hold. If a hibernation ever
  // did land mid-upload the chunks would be gone and the next chunk would
  // meet a null `upload` — which already answers "bad-frame" and closes,
  // a clean refusal rather than a silently truncated replay.
  hydrate(ws) {
    if (this.hydrated) return;
    this.hydrated = true;
    let a = null;
    try { a = ws.deserializeAttachment(); } catch (e) {}
    if (!a) return;
    // Coerce every counter. A field this code did not write — an attachment
    // from a different version of the shape, say — would otherwise restore
    // `undefined`, and `++undefined` is NaN, which compares false against
    // EVERY cap: the per-connection budget would silently stop existing.
    // A guard must not fail open because a field went missing.
    this.ip = typeof a.ip === "string" ? a.ip : this.ip;
    this.dev = a.dev === true;
    this.queries = Number(a.queries) || 0;
    this.submits = Number(a.submits) || 0;
    this.fetches = Number(a.fetches) || 0;
    this.forced_reject_once_ = a.forced === true;
  }

  persist(ws) {
    try {
      ws.serializeAttachment({
        ip: this.ip, dev: this.dev, queries: this.queries,
        submits: this.submits, fetches: this.fetches,
        forced: !!this.forced_reject_once_,
      });
    } catch (e) {}
  }

  async fetch(request) {
    const url = new URL(request.url);
    if (url.pathname !== "/connect")
      return new Response("not found", { status: 404 });
    this.ip = url.searchParams.get("ip") || "local";
    this.dev = url.searchParams.get("dev") === "1";
    const pair = new WebSocketPair();
    this.state.acceptWebSocket(pair[1]);
    this.hydrated = true;  // this instance IS the connection's origin
    this.persist(pair[1]);
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
    this.hydrate(ws);
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
      //
      // Only CANONICAL seasons are listed — the deliberate SEASON-file
      // stamps (s1, s2, ...). Dev builds stamp git-describe strings and
      // their submissions are admitted (an old best charts in its own
      // bucket), but those one-off buckets must not clutter the browser
      // every player cycles through. A build browsing its own non-listed
      // season still can: the client seeds its own and best.nrp's
      // seasons into the list locally.
      if (++this.queries > CONN_MAX_QUERIES) return this.fail(ws, "rate-limited");
      this.persist(ws);
      if (!(await within_limit(this.env, this.ip, "query", this.dev)))
        return this.err(ws, "rate-limited");
      await ensure_schema(this.env.DB);
      const rows = await this.env.DB.prepare(
          `SELECT season, MAX(submitted_at) AS newest, COUNT(*) AS n
           FROM scores GROUP BY season ORDER BY newest DESC LIMIT 200`).all();
      const canonical = (rows.results || [])
          .filter((r) => season_canonical(r.season)).slice(0, 50);
      this.send(ws, {
        t: "seasons",
        rows: canonical.map((r) => ({
          season: r.season, newest: Number(r.newest), count: Number(r.n),
        })),
      });
      return;
    }

    if (msg.t === "qualify" || msg.t === "rank-of" || msg.t === "top") {
      if (++this.queries > CONN_MAX_QUERIES) return this.fail(ws, "rate-limited");
      this.persist(ws);
      // Per-IP aggregate read budget (see LIMITS): reads are cheap to retry,
      // so a refusal is a non-fatal err, not a socket close.
      if (!(await within_limit(this.env, this.ip, "query", this.dev)))
        return this.err(ws, "rate-limited");
      const season = typeof msg.season === "string" ? msg.season : "";
      const players = board_slot(Number(msg.players));
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
          // Echo the board identity (like qualify/rank-of): handlers can
          // answer out of order, so a client flipping SOLO/CO-OP (or
          // cycling seasons) fast needs to drop answers for a board it
          // has moved away from — without this a stale answer landing
          // last left the CO-OP screen showing SOLO's empty row set
          // (field, 2026-08-03).
          season, players,
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
      // On a whitelisted worker (production) a non-canonical season can
      // never be admitted, so it can never place — answering false here
      // means a dev build pointed at production simply never prompts,
      // instead of arming an upload doomed to bad-season.
      const admissible = !canonical_only(this.env) || season_canonical(season);
      this.send(ws, { t: "qualify", place, players, cutline: cut,
                      would_place: admissible && place <= KEEP_N });
      return;
    }

    if (msg.t === "submit") {
      if (this.upload) return this.fail(ws, "bad-frame");
      if (++this.submits > CONN_MAX_SUBMITS) return this.fail(ws, "rate-limited");
      this.persist(ws);
      const size = Number(msg.size) >>> 0;
      // Distinct reasons (matching validate.js's blob-side pair): a
      // below-minimum announcement used to answer "too-large".
      if (size < MIN_SUBMISSION_BYTES) return this.err(ws, "too-small");
      if (size > MAX_SUBMISSION_BYTES) return this.err(ws, "too-large");
      // A refusal, not a close (like the query budget above): closing
      // here made the client render the whole screen as LEADERBOARD
      // UNAVAILABLE instead of the upload row's TRY LATER (field report
      // — the Closed teardown outranked the refusal it arrived with).
      // The per-connection CONN_MAX_SUBMITS cap above still closes: that
      // one is a misbehaving-client guard, not a budget answer.
      if (!(await within_limit(this.env, this.ip, "submit", this.dev)))
        return this.err(ws, "rate-limited");
      // Attestation is an admission requirement (LEADERBOARD.md): verify
      // BEFORE accepting megabytes of chunks — a spoofed submit costs the
      // spoofer the round-trip, not us the bandwidth.
      const platform = Number(msg.platform) >>> 0;
      const identity = await verify_identity(
          this.env, platform, msg.name, msg.cred, this.dev);
      if (!identity) {
        // The COERCED number, not msg.platform: the raw field is whatever
        // the client sent, up to the 32 KB frame cap and including
        // newlines, so interpolating it let a submitter write arbitrary
        // lines into our logs (LEADERBOARD.md S6).
        console.log(`submit refused: unverified platform=${platform}`);
        return this.err(ws, "unverified");
      }
      // DEV/TEST ONLY: force the FIRST submit on a connection to look
      // unverified, so the client's warm-a-fresh-credential-and-retry path
      // (credential-lifecycle hardening) can be exercised without a real
      // single-use collision. The retry (2nd submit, same socket) passes.
      // Never set in production — and, like FAKE_VERIFY, it now cannot BE
      // set there: this.dev demands a loopback host (is_dev_host).
      if (this.dev && this.env.REJECT_FIRST_VERIFY === "1" &&
          !this.forced_reject_once_) {
        this.forced_reject_once_ = true;
        this.persist(ws);
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
      this.persist(ws);
      // Per-IP aggregate fetch budget (LIMITS.fetch): the per-connection
      // cap above resets on reconnect, so without this gate connection
      // churn multiplied R2 reads to conn-limit x CONN_MAX_FETCHES per
      // window — the config existed but no code consulted it (review,
      // 2026-08-01). A refusal is a non-fatal err like the query budget's.
      if (!(await within_limit(this.env, this.ip, "fetch", this.dev)))
        return this.err(ws, "rate-limited");
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
    // Does the file's shape agree with the duration its header claims?
    // Logged, never enforced (LEADERBOARD.md S3): legitimate crash
    // artifacts can disagree, and the check is no defence against the real
    // threat anyway — an edited score leaves a perfectly consistent file.
    // This exists to answer "are fabricated submissions actually showing
    // up?" with evidence instead of a guess, which is the trigger L5 waits
    // on. Both interpolated fields are already bounded: season passed
    // season_ok (printable ASCII, no space) and run_id is decimal digits.
    const shape = shape_note(hd, v.stats);
    if (shape)
      console.log(`submit shape: ${shape} ` +
                  `(season=${hd.season} run=${hd.run_id} score=${hd.score})`);
    if (canonical_only(this.env) && !season_canonical(hd.season)) {
      // Production whitelist: only deliberate SEASON-file seasons are
      // admitted. Beta (no var) stays open for dev/test submissions.
      console.log(`submit refused: bad-season ${hd.season}`);
      return this.err(ws, "bad-season");
    }
    const db = this.env.DB;
    await ensure_schema(db);
    const players = board_slot(hd.player_count);
    const key = await platform_key(identity.account);

    // Same run resubmitted (clean-abandon uploaded, then resumed and
    // improved): upsert only a strictly better score — and only by the
    // SAME account. A different account holding the same run_id (a copied
    // file, or an online peer's pre-split recording — post-split each
    // side records under its own derived id) is refused cleanly: without
    // this it slipped past the score check and died on the (season,
    // run_id) primary key as a raw "internal" error.
    const run_row = await db.prepare(
        `SELECT score, platform_key, players FROM scores
         WHERE season = ?1 AND run_id = ?2`)
        .bind(hd.season, hd.run_id).first();
    if (run_row && (Number(run_row.score) >= hd.score ||
                    run_row.platform_key !== key ||
                    Number(run_row.players) !== players))
      return this.err(ws, "already-submitted");
    // That last clause is the same refusal for a run_id already held on the
    // OTHER board. It cannot be left to the upsert: the ON CONFLICT target
    // is (season, players, platform_key), and when the row it collides with
    // differs in `players` the violated constraint is the (season, run_id)
    // PRIMARY KEY instead — a target SQLite's upsert does not cover, so the
    // statement ABORTS. That threw a raw "internal" after the blob was
    // already stored, orphaning it (S2). Both header fields are
    // attacker-chosen, so it was a repeatable way to grow R2 for free.

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
    // From here the blob EXISTS, so every exit has to account for it. The
    // refusal paths below delete it explicitly; this catch covers the rest
    // (a D1 outage, a constraint nobody predicted) — without it a throw
    // unwound to webSocketMessage's generic handler and left an object no
    // row points at, which the retention cron never looks at because it
    // walks rows. That is a leak that only ever grows (S2). The orphan
    // sweep in scheduled() is the backstop for whatever still slips
    // through; this is the fix for what we can see.
    let won;
    try {
      won = await this.place_row(db, hd, players, key, blob_key, identity,
                                 mine);
    } catch (e) {
      try { await this.env.REPLAYS.delete(blob_key); } catch (e2) {}
      throw e;
    }
    if (!won) return this.err(ws, "not-best");
    const rank = await rank_of(db, hd.season, players, hd.score);
    console.log(`placed: season=${hd.season} players=${players} ` +
                `score=${hd.score} rank=${rank} platform=${identity.platform}`);
    this.send(ws, { t: "placed", rank });
  }

  // The row half of a submission: upsert, decide whether this run won the
  // player's slot, and delete whichever blob lost. Returns false when OUR
  // blob was the loser (already deleted here), so the caller answers
  // not-best instead of placed.
  async place_row(db, hd, players, key, blob_key, identity, mine) {
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
      return true;
    }
    // A concurrent better submission won; our blob is orphaned.
    try { await this.env.REPLAYS.delete(blob_key); } catch (e) {}
    return false;
  }
}
