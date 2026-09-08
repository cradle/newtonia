// Retention-cron unit test (LEADERBOARD.md retention decision): drives the
// real `scheduled()` handler from worker.js against a real SQLite behind a
// D1-shaped adapter (d1_sqlite.mjs) and an in-memory R2 fake. Covers the
// live-season exemption — the season with the newest submission per
// players count is never dormancy-stripped, no matter how old it is —
// alongside the ordinary below-KEEP_N trim and the dormant-season strip;
// then the Workers-review (2026-09-08) properties: rows are demoted BEFORE
// their objects are deleted (F5 ordering — no committed row may point at a
// deleted blob), a run that improved between candidate selection and
// demotion keeps its new replay (F5), and the whole cron plus a snapshot
// publish stays inside the Free plan's 50-queries-per-invocation cap on a
// busy night (F7).
//
// Run: node test/retention_test.mjs
import worker, { ensure_schema } from "../src/worker.js";
import { d1_sqlite, seed, clear_rows, sql_all, sql_get, sql_run }
  from "./d1_sqlite.mjs";

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

await ensure_schema(d1_sqlite());

let next_run = 0;
function row(season, players, score, submitted_at, blob = true) {
  const run_id = String(++next_run);
  return { season, players, run_id, score, submitted_at,
           blob_key: blob ? `${season}/${run_id}.k${run_id}.nrp` : "" };
}

// `objects` seeds what R2 holds, for the orphan sweep ({key, uploaded}).
// delete() takes a key or an array of keys, like the real binding.
function fake_r2(objects = [], opts = {}) {
  return {
    deleted: [],
    puts: {},  // key -> body (the snapshot publish lands here)
    objects: objects.slice(),
    async delete(k) {
      if (opts.fail_delete) throw new Error("R2 unavailable");
      const keys = Array.isArray(k) ? k : [k];
      this.deleted.push(...keys);
      this.objects = this.objects.filter((o) => !keys.includes(o.key));
    },
    async put(k, body) { this.puts[k] = body; },
    async list() { return { objects: this.objects, truncated: false }; },
  };
}

async function run_cron(rows, objects = [], hooks = {}, r2opts = {}) {
  clear_rows();
  seed(rows);
  const r2 = fake_r2(objects, r2opts);
  await worker.scheduled({}, { DB: d1_sqlite(hooks), REPLAYS: r2 });
  return r2;
}

function blobs(season, players) {
  return sql_get(`SELECT COUNT(*) AS n FROM scores
                  WHERE season = ?1 AND players = ?2 AND blob_key != ''`,
                 season, players).n;
}

// 1. Live-season exemption: two ancient solo seasons; only the one with the
// newest submission is live, the other is stripped wholesale.
{
  const rows = [
    row("vA", 1, 500, OLD), row("vA", 1, 400, OLD - DAY),
    row("vB", 1, 300, OLD + DAY), row("vB", 1, 200, OLD),
  ];
  await run_cron(rows);
  check("dormant non-live season stripped", blobs("vA", 1) === 0);
  check("live season keeps blobs despite age", blobs("vB", 1) === 2);
}

// 2. The live season still gets the ordinary below-KEEP_N trim.
{
  const rows = [];
  for (let i = 0; i < 102; i++) rows.push(row("vB", 1, 1000 - i, OLD));
  const r2 = await run_cron(rows);
  check("live season trimmed to KEEP_N blobs", blobs("vB", 1) === 100);
  check("trim deleted the demoted blobs from R2", r2.deleted.length === 2,
        JSON.stringify(r2.deleted));
  const kept = sql_all(`SELECT score FROM scores WHERE blob_key != ''`);
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
  check("fresh non-live season keeps blobs", blobs("vA", 1) === 1);
}

// 4. Liveness is per players count: a season dead on the solo board is
// still the live co-op season if co-op's newest submission is there.
{
  const rows = [
    row("vA", 1, 500, OLD), row("vB", 1, 300, OLD + DAY),  // live solo: vB
    row("vA", 2, 800, OLD),                                 // live co-op: vA
  ];
  await run_cron(rows);
  check("solo board of the old season stripped", blobs("vA", 1) === 0);
  check("same season live on co-op keeps blobs", blobs("vA", 2) === 1);
}

// 4b. Rank among ALL rows, not just blob-bearing ones: a blob row whose
// true rank is past KEEP_N must go even when score-only rows sit above it.
{
  const rows = [];
  for (let i = 0; i < 100; i++) rows.push(row("vB", 1, 1000 - i, FRESH, false));
  rows.push(row("vB", 1, 1, FRESH));  // rank 101, the only blob
  await run_cron(rows);
  check("blob ranked past KEEP_N under score-only rows is demoted",
        blobs("vB", 1) === 0);
}

// 5. Orphan sweep (S2): objects no row points at are deleted, but only once
// they are past the grace period — a submission in flight has stored its
// blob and not yet inserted its row, and deleting that would break a live
// upload. Referenced blobs are never touched.
{
  const rows = [row("vB", 1, 500, FRESH)];  // live season, keeps its blob
  const r2 = await run_cron(rows, [
    { key: rows[0].blob_key, uploaded: new Date(FRESH).toISOString() },
    { key: "vB/orphan-old.nrp", uploaded: new Date(NOW - 2 * DAY).toISOString() },
    { key: "vB/orphan-new.nrp", uploaded: new Date(NOW - 60 * 1000).toISOString() },
    { key: "vB/orphan-nodate.nrp" },
  ]);
  check("stale orphan swept", r2.deleted.includes("vB/orphan-old.nrp"),
        JSON.stringify(r2.deleted));
  check("in-flight upload spared",
        !r2.deleted.includes("vB/orphan-new.nrp"), JSON.stringify(r2.deleted));
  check("undated object spared",
        !r2.deleted.includes("vB/orphan-nodate.nrp"), JSON.stringify(r2.deleted));
  check("referenced blob untouched",
        !r2.deleted.includes(rows[0].blob_key), JSON.stringify(r2.deleted));
}

// 6. Site snapshot publish: every cron run rewrites site/leaderboard.json
// with the canonical seasons only, live flags per players count, and rows
// that survived this run's retention. The snapshot object itself must be
// exempt from the orphan sweep (no row points at it) no matter how old.
{
  const rows = [
    row("s1", 1, 500, FRESH), row("s1", 1, 400, FRESH - DAY),
    row("s2", 1, 300, FRESH + DAY),          // live solo season
    row("s1", 2, 800, FRESH),                 // live (only) co-op season
    row("vdev-abc", 1, 999, FRESH + 2 * DAY), // non-canonical: never listed
  ];
  const r2 = await run_cron(rows, [
    { key: "site/leaderboard.json",
      uploaded: new Date(NOW - 30 * DAY).toISOString() },
    // `site` is a legal season key (season_ok allows it), so a submission
    // can land a blob INSIDE the snapshot's prefix. The sweep's skip must
    // be the exact snapshot key, not the prefix — otherwise that whole
    // namespace escapes the orphan backstop forever (security review F2).
    { key: "site/424242.nrp",
      uploaded: new Date(NOW - 2 * DAY).toISOString() },
  ]);
  check("old snapshot object spared by orphan sweep",
        !r2.deleted.includes("site/leaderboard.json"),
        JSON.stringify(r2.deleted));
  check("orphan inside the site/ prefix is still swept",
        r2.deleted.includes("site/424242.nrp"), JSON.stringify(r2.deleted));
  const snap = JSON.parse(r2.puts["site/leaderboard.json"] || "null");
  check("snapshot published", !!snap && Array.isArray(snap.boards));
  const key = (b) => `${b.season}/${b.players}`;
  const names = snap.boards.map(key).sort();
  check("snapshot lists canonical boards only",
        JSON.stringify(names) === JSON.stringify(["s1/1", "s1/2", "s2/1"]),
        JSON.stringify(names));
  const live = snap.boards.filter((b) => b.live).map(key).sort();
  check("live flag follows newest submission per players count",
        JSON.stringify(live) === JSON.stringify(["s1/2", "s2/1"]),
        JSON.stringify(live));
  const s1 = snap.boards.find((b) => b.season === "s1" && b.players === 1);
  check("rows ranked by score", s1.rows.length === 2 &&
        s1.rows[0].rank === 1 && s1.rows[0].score === 500 &&
        s1.rows[1].score === 400, JSON.stringify(s1 && s1.rows));
  check("rows carry the watch fields",
        s1.rows[0].has_replay === true &&
        typeof s1.rows[0].run_id === "string" &&
        typeof s1.rows[0].date === "number");
  check("seasons ordered newest-first",
        snap.boards[0].season === "s2", JSON.stringify(snap.boards.map(key)));
  check("snapshot carries the data watermark",
        snap.watermark === FRESH + 2 * DAY, `${snap.watermark}`);
}

// 7. F5 — a run that IMPROVES between candidate selection and demotion
// keeps its new replay. The improvement (a new score under a NEW per-upload
// key, as finish_submit writes it) lands right after the cron's candidate
// query has returned and before its UPDATE runs. The stale key we selected
// is deleted (nothing points at it any more); the new one is untouched.
{
  const rows = [];
  for (let i = 0; i < 100; i++) rows.push(row("vB", 1, 1000 - i, FRESH));
  const victim = row("vB", 1, 1, FRESH);           // rank 101: selected
  rows.push(victim);
  const NEW_KEY = "vB/improved.k9.nrp";
  let improved = false;
  const r2 = await run_cron(rows, [
    { key: victim.blob_key, uploaded: new Date(FRESH).toISOString() },
    { key: NEW_KEY, uploaded: new Date(NOW).toISOString() },
  ], {
    after: async (sql) => {
      if (improved || !sql.includes("live_newest")) return;
      improved = true;
      sql_run(`UPDATE scores SET score = 9999, blob_key = ?1, submitted_at = ?2
               WHERE season = 'vB' AND run_id = ?3`, NEW_KEY, NOW, victim.run_id);
    },
  });
  const v = sql_get(`SELECT score, blob_key FROM scores
                     WHERE season = 'vB' AND run_id = ?1`, victim.run_id);
  check("improved-after-selection row keeps its new replay",
        v.score === 9999 && v.blob_key === NEW_KEY, JSON.stringify(v));
  check("the improved row's object is not deleted",
        !r2.deleted.includes(NEW_KEY), JSON.stringify(r2.deleted));
  check("the stale selected key is reclaimed",
        r2.deleted.includes(victim.blob_key), JSON.stringify(r2.deleted));
}

// 8. F5 ORDERING — rows are demoted before their objects are deleted, so
// a failure between the two never leaves a row pointing at nothing. With
// R2 refusing the delete, every demoted row is still score-only (the
// objects linger as orphans for the sweep); with D1 refusing the UPDATE,
// nothing is deleted from R2 at all.
{
  const rows = [];
  for (let i = 0; i < 101; i++) rows.push(row("vB", 1, 1000 - i, FRESH));
  const r2 = await run_cron(rows, [], {}, { fail_delete: true });
  check("R2 delete failure: rows still demoted", blobs("vB", 1) === 100);
  check("R2 delete failure: nothing recorded deleted", r2.deleted.length === 0);
}
{
  const rows = [];
  for (let i = 0; i < 101; i++) rows.push(row("vB", 1, 1000 - i, FRESH));
  let r2 = null;
  let threw = false;
  try {
    r2 = await run_cron(rows, [], {
      before: async (sql) => {
        if (sql.startsWith("UPDATE scores SET blob_key = ''"))
          throw new Error("D1 unavailable");
      },
    });
  } catch (e) { threw = true; }
  check("D1 UPDATE failure surfaces", threw);
  const dangling = sql_all(`SELECT blob_key FROM scores WHERE blob_key != ''`)
      .filter((r) => r2 && r2.deleted.includes(r.blob_key));
  check("D1 UPDATE failure: no row points at a deleted object",
        dangling.length === 0 && blobs("vB", 1) === 101);
}

// 9. F7 — the whole invocation (retention + orphan sweep + snapshot
// publish) stays under the Free plan's 50 D1 queries: a busy board (150
// blob rows past the cut, i.e. two UPDATE chunks) and 30 canonical seasons
// with both boards populated (which used to cost 1 + 2 x 30 queries for
// the snapshot alone).
{
  const rows = [];
  for (let i = 0; i < 250; i++) rows.push(row("s1", 1, 1000 - i, FRESH));
  for (let s = 2; s <= 30; s++) {
    rows.push(row(`s${s}`, 1, 100, FRESH - s * DAY));
    rows.push(row(`s${s}`, 2, 100, FRESH - s * DAY));
  }
  let queries = 0;
  const r2 = await run_cron(rows, [], { before: async () => { queries++; } });
  check("busy cron stays under 50 D1 queries", queries < 50, `${queries}`);
  check("busy cron demoted everything past the cut", blobs("s1", 1) === 100);
  const snap = JSON.parse(r2.puts["site/leaderboard.json"] || "null");
  check("30-season snapshot published in the same invocation",
        !!snap && snap.boards.length === 59, snap && `${snap.boards.length}`);
  check("snapshot boards hold at most KEEP_N rows",
        snap.boards.every((b) => b.rows.length <= 100));
}

// 10. F7 — the per-run demotion cap: a backlog past it is left for the
// next night rather than blowing the query budget, and the next run
// finishes it.
{
  const rows = [];
  for (let i = 0; i < 400; i++) rows.push(row("s1", 1, 1000 - i, FRESH));
  const r2 = await run_cron(rows);
  check("first run demotes the per-run cap", r2.deleted.length === 180,
        `${r2.deleted.length}`);
  await worker.scheduled({}, { DB: d1_sqlite(), REPLAYS: fake_r2() });
  check("second run finishes the backlog", blobs("s1", 1) === 100,
        `${blobs("s1", 1)}`);
}

console.log(failures ? `${failures} FAILURE(S)` : "ALL PASS");
process.exit(failures ? 1 : 0);
