// Unit test for the site-snapshot rebuild guards (security review
// 2026-08-04, F1; Workers review 2026-09-08, F6/F8). A rebuild is ~10,000x
// an ordinary view and is reachable from an unauthenticated GET, so these
// properties have to hold:
//
//   1. SINGLE-FLIGHT — concurrent stale views share ONE rebuild. Without
//      it every view inside a stale window ran its own full rebuild
//      (a ranked scan of the table plus an R2 write), so one IP's read
//      budget could burn millions of D1 rows.
//   2. STALE-ON-FAILURE — a failing rebuild serves the stale body instead
//      of 500ing, and then backs off. The original code cleared the body
//      BEFORE rebuilding, so a persistent failure returned 500 and left
//      the snapshot stale, making every later view retry forever.
//   3. COLD-MISS BACKOFF (F8) — the same backoff holds with NO stored
//      snapshot: a failing rebuild answers 503 once and then 503 without
//      re-running the failing build until the cooldown lapses. It used to
//      retry on every request.
//   4. COMMIT-ORDERED REVISION (F6, tightened by the review of PR #527) —
//      freshness compares the board's revision (meta.rev, bumped in the
//      same D1 batch as every score mutation) with the revision the build's
//      read SAW — never a timestamp. A completion time hid a score
//      committed mid-build; MAX(submitted_at) hid a score whose Date.now()
//      stamp was taken BEFORE its write and committed after the read.
//
// None of this is reachable from the protocol test: `wrangler dev --local`
// serialises requests, so concurrency cannot be observed there. This drives
// the real `fetch` handler against a real SQLite (d1_sqlite.mjs) and an
// in-memory R2 fake instead.
//
// Run: node test/snapshot_guard_test.mjs
import worker, { ensure_schema, Session } from "../src/worker.js";
import { d1_sqlite, seed, clear_rows, sql_run, sql_get } from "./d1_sqlite.mjs";
import { build_nrp } from "./nrp_fixture.mjs";

let failures = 0;
function check(name, cond, detail) {
  console.log((cond ? "PASS " : "FAIL ") + name +
              (cond || detail === undefined ? "" : `  (${detail})`));
  if (!cond) failures++;
}

// The rebuild backoff is module state in the worker; tests move the clock
// past it instead of waiting a minute.
const real_now = Date.now;
let clock_skew = 0;
Date.now = () => real_now() + clock_skew;
function advance(ms) { clock_skew += ms; }

const NOW = Date.now();

await ensure_schema(d1_sqlite());

// One row on the s1 solo board; `submit()` is what a fresh submission
// looks like to the freshness probe: a row change plus the revision bump
// the worker batches with every score mutation.
function reset_rows(newest = NOW, score = 500) {
  clear_rows();
  seed([{ season: "s1", players: 1, run_id: "1", score, submitted_at: newest,
          blob_key: "s1/1.k1.nrp", format: 2, save_format: 17 }]);
}
function submit(ms, score) {
  sql_run(`UPDATE scores SET submitted_at = ?1` +
          (score !== undefined ? `, score = ${Number(score)}` : ``), ms);
  sql_run(`UPDATE meta SET v = v + 1 WHERE k = 'rev'`);
}
const rev_now = () => sql_get(`SELECT v FROM meta WHERE k = 'rev'`).v;
// A mutation that removes rows (a test clearing the table) — bump so the
// probe notices, as the worker's own mutations do.
function submit_none() { sql_run(`UPDATE meta SET v = v + 1 WHERE k = 'rev'`); }

// Hooks count full builds (the ranked read is once per build) and probes,
// and inject failures on demand.
function fake_db(state) {
  return d1_sqlite({
    before: async (sql) => {
      if (sql.includes("ROW_NUMBER() OVER") && sql.includes("AS rev")) {
        state.builds++;
        if (state.fail_builds) throw new Error("D1 unavailable");
        if (state.before_read) { const f = state.before_read; state.before_read = null; await f(); }
      }
      if (sql.startsWith("SELECT v FROM meta")) state.probes++;
    },
    after: async (sql) => {
      if (sql.includes("ROW_NUMBER() OVER") && sql.includes("AS rev") &&
          state.after_read) {
        const f = state.after_read;
        state.after_read = null;
        await f();
      }
    },
  });
}

function fake_r2(state) {
  return {
    async get(key) {
      const o = state.objects[key];
      if (!o) return null;
      return {
        text: async () => o.body,
        customMetadata: o.customMetadata,
      };
    },
    async put(key, body, opts) {
      if (state.fail_puts) throw new Error("R2 unavailable");
      state.objects[key] = { body, customMetadata: (opts || {}).customMetadata };
    },
    async delete() {},
    async list() { return { objects: [], truncated: false }; },
  };
}

// A Limiter that always allows, so the guards are what's under test.
function fake_env(state) {
  return {
    DB: fake_db(state),
    REPLAYS: fake_r2(state),
    LIMITS: { idFromName: (n) => n, get: () => ({
      fetch: async () => new Response(JSON.stringify({ allowed: true }),
                                      { headers: { "Content-Type": "application/json" } }),
    }) },
  };
}

function view(env) {
  return worker.fetch(
      new Request("https://board.example/site/leaderboard.json"), env);
}

function fresh_state(over = {}) {
  advance(120_000);  // past any backoff an earlier case armed
  reset_rows();
  return { objects: {}, builds: 0, probes: 0, after_read: null,
           before_read: null, fail_builds: false, fail_puts: false, ...over };
}

// 1. Cold miss builds once and stores with its build time + revision in
// metadata.
{
  const state = fresh_state();
  const env = fake_env(state);
  const r = await view(env);
  check("cold miss serves 200", r.status === 200, `HTTP ${r.status}`);
  const snap = await r.json();
  check("cold miss built the snapshot", state.builds === 1, `${state.builds}`);
  check("cold miss skips the probe (nothing to compare)", state.probes === 0,
        `${state.probes}`);
  const meta = (state.objects["site/leaderboard.json"] || {}).customMetadata || {};
  check("snapshot stored with build time", !!meta.generated_at);
  check("snapshot stored with the board revision", meta.rev === String(rev_now()),
        `${meta.rev} vs ${rev_now()}`);
  check("snapshot has the season's rows",
        snap.boards.length === 1 && snap.boards[0].rows.length === 1,
        JSON.stringify(snap.boards));
}

// 2. A fresh snapshot is served straight from R2 — probe only, no rebuild.
{
  const state = fresh_state();
  const env = fake_env(state);
  await view(env);
  const before = state.builds;
  await view(env);
  await view(env);
  check("fresh views do not rebuild", state.builds === before, `${state.builds}`);
  check("fresh views do run the probe", state.probes === 2, `${state.probes}`);
}

// 3. SINGLE-FLIGHT: many concurrent stale views cost ONE rebuild.
{
  const state = fresh_state();
  const env = fake_env(state);
  await view(env);                    // seed a stored snapshot
  submit(NOW + 60_000);               // a submission lands -> stale
  const before = state.builds;
  const rs = await Promise.all(Array.from({ length: 25 }, () => view(env)));
  check("all concurrent views answered 200",
        rs.every((r) => r.status === 200));
  check("25 concurrent stale views cost ONE rebuild",
        state.builds - before === 1, `${state.builds - before} rebuilds`);
  const bodies = await Promise.all(rs.map((r) => r.json()));
  check("every concurrent view got the REBUILT snapshot",
        bodies.every((b) => b.rev === rev_now()));
}

// 4. A later submission still rebuilds immediately — the guard must not
// delay a SUCCESSFUL refresh (the freshness property the probe exists for).
{
  const state = fresh_state();
  const env = fake_env(state);
  await view(env);
  submit(NOW + 1000);
  await view(env);
  const after_first = state.builds;
  submit(NOW + 2000);                 // another submission, moments later
  await view(env);
  check("a new submission rebuilds again with no cooldown",
        state.builds === after_first + 1, `${state.builds}`);
}

// 5. STALE-ON-FAILURE: a failing rebuild serves the stale body, not a 500,
// and then stops retrying for the backoff window.
{
  const state = fresh_state();
  const env = fake_env(state);
  await view(env);                    // store a good snapshot
  const good = await (await view(env)).json();
  submit(NOW + 60_000);               // stale
  state.fail_builds = true;           // ...and rebuilding now throws
  const r = await view(env);
  check("failed rebuild still serves 200", r.status === 200, `HTTP ${r.status}`);
  const served = await r.json();
  check("failed rebuild serves the STALE body",
        served.generated_at === good.generated_at,
        `${served.generated_at} vs ${good.generated_at}`);
  const after_fail = state.builds;
  await view(env);
  await view(env);
  check("further views back off instead of retrying the failing rebuild",
        state.builds === after_fail, `${state.builds - after_fail} retries`);
}

// 6. An R2 write failure is the same story (the build succeeds, the store
// doesn't) — and with NO stored snapshot at all, a failure is a clean 503
// rather than an unhandled throw.
{
  const state = fresh_state();
  const env = fake_env(state);
  await view(env);
  submit(NOW + 60_000);
  state.fail_puts = true;
  const r = await view(env);
  check("failed STORE still serves the stale body", r.status === 200,
        `HTTP ${r.status}`);
}
{
  const state = fresh_state({ fail_builds: true });
  const env = fake_env(state);
  const r = await view(env);
  check("cold miss + failing rebuild answers 503, not a raw throw",
        r.status === 503, `HTTP ${r.status}`);
}

// 7. F8 — COLD-MISS BACKOFF: after that first failure the cooldown holds
// with nothing stored too. Sequential requests answer 503 at once with a
// Retry-After, spending no further build attempts; once the cooldown
// lapses the next request tries again.
{
  const state = fresh_state({ fail_builds: true });
  const env = fake_env(state);
  const first = await view(env);
  check("cold miss: first failing request answers 503", first.status === 503,
        `HTTP ${first.status}`);
  const attempted = state.builds;
  const r2 = await view(env);
  const r3 = await view(env);
  check("cold miss: later requests answer 503 without rebuilding",
        r2.status === 503 && r3.status === 503 && state.builds === attempted,
        `${state.builds - attempted} extra attempts`);
  check("cold miss: backed-off 503 carries Retry-After",
        Number(r2.headers.get("Retry-After")) > 0, r2.headers.get("Retry-After"));
  advance(61_000);
  state.fail_builds = false;
  const r4 = await view(env);
  check("cold miss: cooldown lapsed -> rebuilt and served",
        r4.status === 200 && state.builds === attempted + 1,
        `HTTP ${r4.status}, ${state.builds - attempted} attempts`);
}

// 8. F6 — a submission that commits AFTER the build's read but BEFORE the
// build finishes is missing from that snapshot; the very next view must
// notice (rev advanced past the one the build read) and rebuild with it,
// instead of trusting a completion timestamp that post-dates the
// submission.
{
  const state = fresh_state();
  const env = fake_env(state);
  await view(env);                     // a stored snapshot at the current rev
  submit(NOW + 1000);                  // stale -> the next view rebuilds
  // ...and DURING that rebuild (right after its read) a better score lands.
  state.after_read = () => submit(NOW + 1500, 999);
  const mid = await (await view(env)).json();
  check("mid-build submission is absent from that build (by construction)",
        mid.boards[0].rows[0].score === 500, JSON.stringify(mid.boards));
  const builds = state.builds;
  const next = await (await view(env)).json();
  check("next view detects the mid-build submission and rebuilds",
        state.builds === builds + 1, `${state.builds - builds} rebuilds`);
  check("the rebuilt snapshot carries the mid-build score",
        next.boards[0].rows[0].score === 999, JSON.stringify(next.boards));
  check("revision advanced to the one the rebuild read",
        next.rev === rev_now(), `${next.rev} vs ${rev_now()}`);
  const again = state.builds;
  await view(env);
  check("and then the snapshot is current again (no rebuild)",
        state.builds === again, `${state.builds - again}`);
}

// 9. A snapshot from a deploy that wrote no revision (or only the earlier
// submitted_at watermark) is stale ONCE and self-heals (store_snapshot
// always writes the field).
{
  const state = fresh_state();
  const env = fake_env(state);
  await view(env);
  const key = "site/leaderboard.json";
  state.objects[key].customMetadata = { generated_at: String(NOW),
                                        watermark: String(NOW) };
  const before = state.builds;
  await view(env);
  check("legacy snapshot without a revision rebuilds once",
        state.builds === before + 1, `${state.builds - before}`);
  await view(env);
  check("...and is then current", state.builds === before + 1);
}

// 10. Review of PR #527 — an OLDER timestamp committing AFTER the read. Two
// real submissions through Session.finish_submit: A takes its Date.now()
// stamp, is paused right before its INSERT; the clock moves on; B commits
// and a view builds the snapshot (B only); A is released and commits with
// its older stamp. MAX(submitted_at) is unchanged by A's commit, so a
// timestamp watermark called the snapshot current forever; the revision
// advanced with A's commit, so the next view rebuilds with both rows.
{
  const state = fresh_state();
  const env = fake_env(state);
  clear_rows();
  const gate = { promise: null, resolve: null };
  gate.promise = new Promise((r) => { gate.resolve = r; });
  const r2 = { async put() {}, async delete() {}, async get() { return null; },
               async list() { return { objects: [], truncated: false }; } };
  const session = (hooks) => {
    const s = new Session({}, { DB: d1_sqlite(hooks), REPLAYS: r2 });
    s.hydrated = true; s.dev = true; return s;
  };
  const ws = () => ({ sent: [], send(x) { this.sent.push(JSON.parse(x)); }, close() {} });
  const ident = (n) => ({ platform: 2, name: n, verified: true, account: `fake:${n}` });
  const A = session({ before: async (sql) => {
    if (sql.startsWith("INSERT")) await gate.promise; } });
  const B = session();
  const wsA = ws(), wsB = ws();
  const pA = A.finish_submit(wsA, build_nrp({ game_version: "s1", run_id: 11n,
                                              score: 700 }), ident("ALICE"));
  await new Promise((r) => setTimeout(r, 20));   // A is parked at its INSERT
  advance(100);                                  // ...and time moves on
  await B.finish_submit(wsB, build_nrp({ game_version: "s1", run_id: 12n,
                                         score: 600 }), ident("BOB"));
  const mid = await (await view(env)).json();
  check("older-stamp race: snapshot built between the two commits shows B only",
        mid.boards[0].rows.length === 1 && mid.boards[0].rows[0].score === 600,
        JSON.stringify(mid.boards));
  gate.resolve();
  await pA;
  check("older-stamp race: A committed with the OLDER stamp",
        wsA.sent.some((f) => f.t === "placed") &&
        sql_get(`SELECT MAX(submitted_at) AS m FROM scores`).m <
            sql_get(`SELECT submitted_at FROM scores WHERE run_id = '12'`).submitted_at + 1 &&
        sql_get(`SELECT submitted_at FROM scores WHERE run_id = '11'`).submitted_at <
            sql_get(`SELECT submitted_at FROM scores WHERE run_id = '12'`).submitted_at,
        JSON.stringify(wsA.sent));
  const builds = state.builds;
  const next = await (await view(env)).json();
  check("older-stamp race: the next view rebuilds anyway",
        state.builds === builds + 1, `${state.builds - builds} rebuilds`);
  check("older-stamp race: the rebuilt snapshot carries both rows",
        next.boards[0].rows.length === 2 &&
        next.boards[0].rows[0].score === 700, JSON.stringify(next.boards));
}

// 11. Review of PR #527, round 2 — the EMPTY board. The revision must come
// from the same statement as the (absent) rows: with a second statement
// for the empty case, the FIRST submission could commit between the two
// and the snapshot carried its revision with no boards, served as current
// until the next mutation. Here the first upload commits right AFTER the
// build's single read; the snapshot is empty at the revision it read, and
// the next view rebuilds with the row.
{
  const state = fresh_state();
  const env = fake_env(state);
  clear_rows();
  const r2 = { async put() {}, async delete() {}, async get() { return null; },
               async list() { return { objects: [], truncated: false }; } };
  const first = async () => {
    const s = new Session({}, { DB: d1_sqlite(), REPLAYS: r2 });
    s.hydrated = true; s.dev = true;
    const w = { sent: [], send(x) { this.sent.push(JSON.parse(x)); }, close() {} };
    await s.finish_submit(w, build_nrp({ game_version: "s1", run_id: 21n, score: 400 }),
                          { platform: 2, name: "FIRST", verified: true, account: "fake:FIRST" });
    return w.sent.some((f) => f.t === "placed");
  };
  let placed = false;
  state.after_read = async () => { placed = await first(); };
  const empty = await (await view(env)).json();
  check("empty board: the first upload landed after the build's read", placed);
  check("empty board: that build is empty (by construction)",
        empty.boards.length === 0, JSON.stringify(empty.boards));
  check("empty board: its revision is the one BEFORE the first upload",
        empty.rev === rev_now() - 1, `${empty.rev} vs ${rev_now()}`);
  const builds = state.builds;
  const next = await (await view(env)).json();
  check("empty board: the next view rebuilds", state.builds === builds + 1,
        `${state.builds - builds}`);
  check("empty board: the rebuilt snapshot carries the first row",
        next.boards.length === 1 && next.boards[0].rows[0].score === 400,
        JSON.stringify(next.boards));
  // And a truly empty board is served as such, at the current revision.
  clear_rows();
  submit_none();
  const again = await (await view(env)).json();
  check("empty board: served empty at the live revision",
        again.boards.length === 0 && again.rev === rev_now(), JSON.stringify(again));
}

Date.now = real_now;
console.log(failures ? `${failures} FAILURE(S)` : "ALL PASS");
process.exit(failures ? 1 : 0);
