// Unit test for the site-snapshot rebuild guards (security review
// 2026-08-04, F1). A rebuild is ~10,000x an ordinary view and is reachable
// from an unauthenticated GET, so two properties have to hold:
//
//   1. SINGLE-FLIGHT — concurrent stale views share ONE rebuild. Without
//      it every view inside a stale window ran its own full rebuild
//      (a scan plus up to 50 seasons x 2 boards x KEEP_N rows, plus an R2
//      write), so one IP's read budget could burn millions of D1 rows.
//   2. STALE-ON-FAILURE — a failing rebuild serves the stale body instead
//      of 500ing, and then backs off. The original code cleared the body
//      BEFORE rebuilding, so a persistent failure returned 500 and left
//      the snapshot stale, making every later view retry forever.
//
// Neither is reachable from the protocol test: `wrangler dev --local`
// serialises requests, so concurrency cannot be observed there. This drives
// the real `fetch` handler against in-memory D1/R2 fakes instead.
//
// Run: node test/snapshot_guard_test.mjs
import worker from "../src/worker.js";

let failures = 0;
function check(name, cond, detail) {
  console.log((cond ? "PASS " : "FAIL ") + name +
              (cond || detail === undefined ? "" : `  (${detail})`));
  if (!cond) failures++;
}

const NOW = Date.now();

// A D1 fake answering the shapes serve_snapshot + build_site_snapshot use,
// counting how many full builds ran (the GROUP BY is once per build).
function fake_db(state) {
  return {
    batch: async () => [],
    prepare(sql) {
      let a = [];
      return {
        bind(...args) { a = args; return this; },
        async first() {
          if (sql.includes("MAX(submitted_at) AS newest")) {
            state.probes++;
            return { newest: state.newest };
          }
          throw new Error("unexpected first(): " + sql);
        },
        async all() {
          if (sql.includes("GROUP BY season")) {
            state.builds++;
            if (state.fail_builds) throw new Error("D1 unavailable");
            // One canonical season so the snapshot is non-trivial.
            return { results: [{ season: "s1", newest: state.newest, n: 1 }] };
          }
          if (sql.includes("save_format")) {
            return { results: a[1] === 1 ? [{
              run_id: "1", score: 500, generation: 3, duration_ms: 1000,
              submitted_at: state.newest, name: "P", platform: 2,
              verified: 1, blob_key: "s1/1.nrp", format: 2, save_format: 17,
            }] : [] };
          }
          throw new Error("unexpected all(): " + sql);
        },
        async run() {},
      };
    },
  };
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
  return { objects: {}, newest: NOW, builds: 0, probes: 0,
           fail_builds: false, fail_puts: false, ...over };
}

// 1. Cold miss builds once and stores with its build time in metadata.
{
  const state = fresh_state();
  const env = fake_env(state);
  const r = await view(env);
  check("cold miss serves 200", r.status === 200, `HTTP ${r.status}`);
  const snap = await r.json();
  check("cold miss built the snapshot", state.builds === 1, `${state.builds}`);
  check("cold miss skips the probe (nothing to compare)", state.probes === 0,
        `${state.probes}`);
  check("snapshot stored with build time",
        !!(state.objects["site/leaderboard.json"] || {}).customMetadata
            ?.generated_at);
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
  state.newest = NOW + 60_000;        // a submission lands -> stale
  const before = state.builds;
  const rs = await Promise.all(Array.from({ length: 25 }, () => view(env)));
  check("all concurrent views answered 200",
        rs.every((r) => r.status === 200));
  check("25 concurrent stale views cost ONE rebuild",
        state.builds - before === 1, `${state.builds - before} rebuilds`);
  const bodies = await Promise.all(rs.map((r) => r.json()));
  check("every concurrent view got the REBUILT snapshot",
        bodies.every((b) => b.generated_at === bodies[0].generated_at));
}

// 4. A later submission still rebuilds immediately — the guard must not
// delay a SUCCESSFUL refresh (the freshness property the probe exists for).
{
  const state = fresh_state();
  const env = fake_env(state);
  await view(env);
  state.newest = NOW + 1000;
  await view(env);
  const after_first = state.builds;
  state.newest = NOW + 2000;          // another submission, moments later
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
  state.newest = NOW + 60_000;        // stale
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
  state.newest = NOW + 60_000;
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

console.log(failures ? `${failures} FAILURE(S)` : "ALL PASS");
process.exit(failures ? 1 : 0);
