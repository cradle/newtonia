// Retention-cron unit test (LEADERBOARD.md retention decision): drives the
// real `scheduled()` handler from worker.js against in-memory D1/R2 fakes
// that answer exactly the SQL shapes the cron issues. Covers the live-season
// exemption — the season with the newest submission per players count is
// never dormancy-stripped, no matter how old it is — alongside the ordinary
// below-KEEP_N trim and the dormant-season strip.
//
// Run: node test/retention_test.mjs
import worker from "../src/worker.js";

let failures = 0;
function check(name, cond, detail) {
  console.log((cond ? "PASS " : "FAIL ") + name +
              (cond || detail === undefined ? "" : `  (${detail})`));
  if (!cond) failures++;
}

const DAY = 24 * 60 * 60 * 1000;
const NOW = Date.now();
const OLD = NOW - 200 * DAY;   // beyond SCORE_ONLY_AFTER_MS (180 d)
const FRESH = NOW - 10 * DAY;  // within it

let next_run = 0;
function row(season, players, score, submitted_at, blob = true) {
  const run_id = String(++next_run);
  return { season, players, run_id, score, submitted_at,
           blob_key: blob ? `${season}/${run_id}.nrp` : "" };
}

// A fake D1 that dispatches on distinctive substrings of the cron's five
// query shapes. Intentionally minimal — an unrecognized query throws so a
// future cron edit fails loudly here instead of silently no-oping.
function fake_db(rows) {
  return {
    batch: async () => [],  // ensure_schema's CREATEs
    prepare(sql) {
      let a = [];
      return {
        bind(...args) { a = args; return this; },
        async first() {
          if (sql.includes("ORDER BY submitted_at DESC LIMIT 1")) {
            const r = rows.filter((x) => x.players === a[0])
                .sort((x, y) => y.submitted_at - x.submitted_at)[0];
            return r ? { season: r.season } : null;
          }
          if (sql.includes("MAX(submitted_at)")) {
            const s = rows.filter(
                (x) => x.season === a[0] && x.players === a[1]);
            if (!s.length) return null;
            const newest = Math.max(...s.map((x) => x.submitted_at));
            return newest < a[2] ? { newest } : null;
          }
          throw new Error("unexpected first(): " + sql);
        },
        async all() {
          if (sql.includes("SELECT DISTINCT")) {
            const seen = new Map();
            for (const r of rows)
              seen.set(`${r.season}\x00${r.players}`,
                       { season: r.season, players: r.players });
            return { results: [...seen.values()] };
          }
          if (sql.includes("blob_key != ''"))
            return { results: rows.filter((r) => r.season === a[0] &&
                r.players === a[1] && r.blob_key !== "") };
          if (sql.includes("OFFSET"))
            return { results: rows.filter((r) => r.season === a[0] &&
                r.players === a[1])
                .sort((x, y) => y.score - x.score ||
                                x.submitted_at - y.submitted_at)
                .slice(a[2]) };
          throw new Error("unexpected all(): " + sql);
        },
        async run() {
          if (sql.includes("UPDATE scores SET blob_key")) {
            for (const r of rows)
              if (r.season === a[0] && r.run_id === a[1]) r.blob_key = "";
            return;
          }
          throw new Error("unexpected run(): " + sql);
        },
      };
    },
  };
}

function fake_r2() { return { deleted: [], async delete(k) { this.deleted.push(k); } }; }

async function run_cron(rows) {
  const r2 = fake_r2();
  await worker.scheduled({}, { DB: fake_db(rows), REPLAYS: r2 });
  return r2;
}

function blobs(rows, season, players) {
  return rows.filter((r) => r.season === season && r.players === players &&
                     r.blob_key !== "").length;
}

// 1. Live-season exemption: two ancient solo seasons; only the one with the
// newest submission is live, the other is stripped wholesale.
{
  const rows = [
    row("vA", 1, 500, OLD), row("vA", 1, 400, OLD - DAY),
    row("vB", 1, 300, OLD + DAY), row("vB", 1, 200, OLD),
  ];
  await run_cron(rows);
  check("dormant non-live season stripped", blobs(rows, "vA", 1) === 0);
  check("live season keeps blobs despite age", blobs(rows, "vB", 1) === 2);
}

// 2. The live season still gets the ordinary below-KEEP_N trim.
{
  const rows = [];
  for (let i = 0; i < 102; i++) rows.push(row("vB", 1, 1000 - i, OLD));
  const r2 = await run_cron(rows);
  check("live season trimmed to KEEP_N blobs", blobs(rows, "vB", 1) === 100);
  check("trim deleted the demoted blobs from R2", r2.deleted.length === 2);
  const kept = rows.filter((r) => r.blob_key !== "");
  check("trim demoted the LOWEST-ranked rows",
        kept.every((r) => r.score >= 901));
}

// 3. A fresh non-live season is not dormant — only trimmed, so a small
// board keeps everything.
{
  const rows = [
    row("vA", 1, 500, FRESH), row("vB", 1, 300, FRESH + DAY),
  ];
  await run_cron(rows);
  check("fresh non-live season keeps blobs", blobs(rows, "vA", 1) === 1);
}

// 4. Liveness is per players count: a season dead on the solo board is
// still the live co-op season if co-op's newest submission is there.
{
  const rows = [
    row("vA", 1, 500, OLD), row("vB", 1, 300, OLD + DAY),  // live solo: vB
    row("vA", 2, 800, OLD),                                 // live co-op: vA
  ];
  await run_cron(rows);
  check("solo board of the old season stripped", blobs(rows, "vA", 1) === 0);
  check("same season live on co-op keeps blobs", blobs(rows, "vA", 2) === 1);
}

console.log(failures ? `${failures} FAILURE(S)` : "ALL PASS");
process.exit(failures ? 1 : 0);
