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
//                       duration_ms,date,has_replay,run_id}]}
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
// Seasons with no submission newer than this lose all blobs (score-only).
const SCORE_ONLY_AFTER_MS = 180 * 24 * 60 * 60 * 1000;

// Chunk size for replay downloads (uploads are client-chunked the same).
const CHUNK = 64 * 1024;

// Display-name bound — mirrors the signal worker's MAX_IDENTITY_NAME and
// the game's NET_IDENTITY_NAME_MAX so no truncation disagreement exists.
const MAX_NAME = 24;
const MAX_CRED = 8192;

// Per-connection budgets (in-DO, no Limiter round-trip per message): a
// socket that exhausts one is closed. Bounds per-message floods the way
// the signal worker's VERIFY_MIN_INTERVAL_MS does.
const CONN_MAX_QUERIES = 120;
const CONN_MAX_SUBMITS = 2;
const CONN_MAX_FETCHES = 5;
// Idle sockets are closed after this long (per-connection DO — nothing to
// hibernate for, the DO dies with the socket).
const CONN_IDLE_MS = 10 * 60 * 1000;

// Per-IP fixed windows (Limiter DO). Connects gate everything upstream;
// submits are the strict one ("a handful per IP per hour").
// SUBMIT_LIMIT is a dev/test var (wrangler dev --var SUBMIT_LIMIT:100) so
// the protocol test's burst of submissions from one IP doesn't trip the
// production window; never set in production.
const LIMITS = {
  conn: { window_ms: 10 * 60 * 1000, limit: 60 },
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
export async function verify_identity(env, platform, name, cred) {
  const claimed = strip_name(name);
  if (env.FAKE_VERIFY) {
    // Dev/e2e shortcut (wrangler dev only): attest the claim without a
    // platform backend. The account derives from the name so two test
    // "players" stay distinct. NEVER set in production.
    return { platform, name: claimed, verified: true,
             account: `fake:${claimed || "anon"}` };
  }
  if (typeof cred !== "string" || !cred || cred.length > MAX_CRED) return null;
  if (platform === 2 /* NET_PLATFORM_STEAM */) {
    const v = await verifySteamTicket(env, cred);
    if (!v) return null;
    return { platform, name: strip_name(v.persona || ""), verified: true,
             account: `steam:${v.steamid}` };
  }
  if (platform === 4 /* NET_PLATFORM_IOS */) {
    const v = await verifyGameCenterCred(env, cred);
    if (!v) return null;
    return { platform, name: claimed, verified: false,
             account: `gc:${v.identifier}` };
  }
  if (platform === 5 /* NET_PLATFORM_ANDROID */) {
    const v = await verifyPlayGamesCode(env, cred);
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
           PRIMARY KEY (season, run_id))`),
    db.prepare(`CREATE INDEX IF NOT EXISTS scores_rank
                ON scores(season, players, score DESC)`),
    db.prepare(`CREATE INDEX IF NOT EXISTS scores_player
                ON scores(season, players, platform_key)`),
  ]);
  schema_ready = true;
}

async function rank_of(db, season, players, score) {
  const r = await db.prepare(
      `SELECT COUNT(*) AS n FROM scores
       WHERE season = ?1 AND players = ?2 AND score > ?3`)
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
  // older than SCORE_ONLY_AFTER_MS.
  async scheduled(event, env) {
    await ensure_schema(env.DB);
    const boards = await env.DB.prepare(
        `SELECT DISTINCT season, players FROM scores`).all();
    let demoted = 0;
    for (const b of boards.results || []) {
      const stale = await env.DB.prepare(
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
              `SELECT run_id, blob_key FROM scores
               WHERE season = ?1 AND players = ?2 AND blob_key != ''
               ORDER BY score DESC, submitted_at ASC LIMIT -1 OFFSET ?3`)
              .bind(b.season, b.players, KEEP_N).all();
      for (const row of rows.results || []) {
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

    if (msg.t === "qualify" || msg.t === "rank-of" || msg.t === "top") {
      if (++this.queries > CONN_MAX_QUERIES) return this.fail(ws, "rate-limited");
      const season = typeof msg.season === "string" ? msg.season : "";
      const players = msg.players === 2 ? 2 : 1;
      if (!season_ok(season)) return this.err(ws, "bad-season");
      await ensure_schema(this.env.DB);
      if (msg.t === "top") {
        const count = Math.min(Math.max(Number(msg.count) || 25, 1), KEEP_N);
        const rows = await this.env.DB.prepare(
            `SELECT run_id, score, generation, duration_ms, submitted_at,
                    name, platform, verified, blob_key
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
          })),
        });
        return;
      }
      const score = Number(msg.score) >>> 0;
      const place = await rank_of(this.env.DB, season, players, score);
      if (msg.t === "rank-of") { this.send(ws, { t: "rank-of", place }); return; }
      const cut = await cutline(this.env.DB, season, players);
      this.send(ws, { t: "qualify", place, cutline: cut,
                      would_place: place <= KEEP_N });
      return;
    }

    if (msg.t === "submit") {
      if (this.upload) return this.fail(ws, "bad-frame");
      if (++this.submits > CONN_MAX_SUBMITS) return this.fail(ws, "rate-limited");
      const size = Number(msg.size) >>> 0;
      if (size < MIN_SUBMISSION_BYTES || size > MAX_SUBMISSION_BYTES)
        return this.err(ws, "too-large");
      if (!(await within_limit(this.env, this.ip, "submit")))
        return this.fail(ws, "rate-limited");
      // Attestation is an admission requirement (LEADERBOARD.md): verify
      // BEFORE accepting megabytes of chunks — a spoofed submit costs the
      // spoofer the round-trip, not us the bandwidth.
      const identity = await verify_identity(
          this.env, Number(msg.platform) >>> 0, msg.name, msg.cred);
      if (!identity) {
        console.log(`submit refused: unverified platform=${msg.platform}`);
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

    // One row per player per season+board: only their best survives.
    const mine = await db.prepare(
        `SELECT run_id, score, blob_key FROM scores
         WHERE season = ?1 AND players = ?2 AND platform_key = ?3
           AND run_id != ?4`)
        .bind(hd.season, players, key, hd.run_id).first();
    if (mine && Number(mine.score) >= hd.score)
      return this.err(ws, "not-best");

    const blob_key = blob_key_for(hd.season, hd.run_id);
    await this.env.REPLAYS.put(blob_key, blob);
    await db.prepare(
        `INSERT OR REPLACE INTO scores(season, players, run_id, score,
           generation, duration_ms, submitted_at, name, platform, verified,
           platform_key, blob_key)
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12)`)
        .bind(hd.season, players, hd.run_id, hd.score, hd.generation,
              hd.duration_ms, Date.now(), identity.name, identity.platform,
              identity.verified ? 1 : 0, key, blob_key).run();
    if (mine) {
      // The superseded personal best goes entirely: row and blob.
      await db.prepare(`DELETE FROM scores WHERE season = ?1 AND run_id = ?2`)
          .bind(hd.season, mine.run_id).run();
      if (mine.blob_key)
        try { await this.env.REPLAYS.delete(mine.blob_key); } catch (e) {}
    }
    const rank = await rank_of(db, hd.season, players, hd.score);
    console.log(`placed: season=${hd.season} players=${players} ` +
                `score=${hd.score} rank=${rank} platform=${identity.platform}`);
    this.send(ws, { t: "placed", rank });
  }
}
