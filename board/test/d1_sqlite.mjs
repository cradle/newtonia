// A D1-shaped adapter over node:sqlite for the standalone board tests.
// The worker's REAL SQL runs against a real SQLite here — the window-ranked
// snapshot and retention reads, the upsert's ON CONFLICT ... WHERE, the
// IN-list demote — instead of the pattern-matched fakes it replaced, which
// answered the query SHAPES the code used to issue and had to be rewritten
// with every SQL change (and could not have caught a wrong query at all).
//
// One in-memory database per process: the worker's ensure_schema runs its
// DDL once per isolate (a module-level flag), so every test shares the
// table it created and `clear_rows()` empties it between cases.
//
// `hooks.before(sql, args)` / `hooks.after(sql, args)` run around every
// statement, async — a test can pause a session at a chosen statement
// (deterministic interleavings), count builds, or throw to inject a D1
// failure. Both receive the statement text, so match on a distinctive
// substring of the query under test.
//
// Node 22.13+ ships node:sqlite unflagged (CI pins 22). The
// ExperimentalWarning it prints is expected.
import { DatabaseSync } from "node:sqlite";

const DB = new DatabaseSync(":memory:");

export function d1_sqlite(hooks = {}) {
  const before = hooks.before || (async () => {});
  const after = hooks.after || (async () => {});
  function stmt(sql) {
    let args = [];
    const self = {
      bind(...a) { args = a; return self; },
      async first() {
        await before(sql, args);
        const r = DB.prepare(sql).get(...args);
        await after(sql, args);
        return r === undefined ? null : { ...r };
      },
      async all() {
        await before(sql, args);
        const rows = DB.prepare(sql).all(...args).map((r) => ({ ...r }));
        await after(sql, args);
        return { results: rows, meta: {} };
      },
      async run() {
        await before(sql, args);
        const r = DB.prepare(sql).run(...args);
        await after(sql, args);
        return { meta: { changes: Number(r.changes) } };
      },
    };
    return self;
  }
  return {
    prepare: stmt,
    // D1 batches are transactions. This runs the statements in order and
    // STOPS at the first failure, which is what the worker's two-statement
    // batches (mutation, then the revision bump) need: no bump without its
    // mutation. It does not wrap them in a real BEGIN/COMMIT, because the
    // hooks may pause one session's statement while another session runs
    // on the same connection — a real transaction would swallow the other
    // session's work into the paused one's.
    async batch(stmts) {
      const out = [];
      for (const s of stmts) out.push(await s.run());
      return out;
    },
  };
}

// Direct access for seeding and asserting (synchronous, no hooks).
export function sql_all(sql, ...args) {
  return DB.prepare(sql).all(...args).map((r) => ({ ...r }));
}
export function sql_get(sql, ...args) {
  const r = DB.prepare(sql).get(...args);
  return r === undefined ? null : { ...r };
}
export function sql_run(sql, ...args) {
  return DB.prepare(sql).run(...args);
}
export function clear_rows() {
  DB.exec("DELETE FROM scores");
}

// Insert score rows with defaults for every NOT NULL column a test does
// not care about. platform_key defaults to one per run_id, so the
// one-row-per-player unique index never trips on seeded data.
let seeded = 0;
export function seed(rows) {
  const ins = DB.prepare(
      `INSERT INTO scores(season, players, run_id, score, generation,
         duration_ms, submitted_at, name, platform, verified, platform_key,
         blob_key, format, save_format)
       VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14)`);
  for (const r of rows) {
    seeded++;
    ins.run(r.season, r.players, String(r.run_id), r.score,
            r.generation ?? 3, r.duration_ms ?? 60000, r.submitted_at,
            r.name ?? "N", r.platform ?? 2, r.verified ?? 1,
            r.platform_key ?? `pk-${r.run_id}-${seeded}`,
            r.blob_key ?? "", r.format ?? 1, r.save_format ?? 16);
  }
}
