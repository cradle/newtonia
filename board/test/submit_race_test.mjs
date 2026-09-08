// Unit test for the row/blob consistency of a submission (security review
// 2026-09-08, F4): two uploads of the SAME run racing each other must leave
// the charting row pointing at the upload that actually won it, the loser
// must delete only its own object, and a same-run resubmission must
// release the superseded object now that every upload has its own key.
// Drives Session.place_row against an in-memory D1 + R2 with the exact
// SQL shapes the worker uses; the interleaving (both upserts before either
// survivor check — the race) is the natural microtask order of two
// concurrent calls, and asserted, not assumed. Pure node.
// Run: node test/submit_race_test.mjs
import { Session } from "../src/worker.js";

let failures = 0;
function check(name, ok, detail) {
  console.log((ok ? "PASS " : "FAIL ") + name + (ok || !detail ? "" : "  " + detail));
  if (!ok) failures++;
}

// D1 fake: the scores table as an array, answering the two statements
// place_row issues. The upsert mirrors the real one's semantics — update
// the player's slot only on a strictly higher score, and abort on the
// (season, run_id) PRIMARY KEY when the collision is with a row on the
// OTHER board (the ON CONFLICT target does not cover it).
function fake_db(rows, log) {
  return {
    // place_row batches the upsert with the revision bump (a D1
    // transaction); run them in order, stopping at the first failure.
    async batch(stmts) {
      const out = [];
      for (const s of stmts) out.push(await s.run());
      return out;
    },
    prepare(sql) {
      const stmt = {
        bind(...a) {
          return {
            async run() {
              if (sql.includes("UPDATE meta SET v = v + 1")) return {};
              if (!sql.includes("INSERT INTO scores")) throw new Error("unexpected run(): " + sql);
              const [season, players, run_id, score, , , , , , , key, blob_key] = a;
              log.push(`upsert ${blob_key}`);
              const slot = rows.find((r) => r.season === season &&
                  r.players === players && r.platform_key === key);
              if (slot) {
                if (score > slot.score)
                  Object.assign(slot, { run_id, score, blob_key });
                return {};
              }
              if (rows.find((r) => r.season === season && r.run_id === run_id))
                throw new Error("D1_ERROR: UNIQUE constraint failed: scores.season, scores.run_id");
              rows.push({ season, players, run_id, score, platform_key: key, blob_key });
              return {};
            },
            async first() {
              if (!sql.includes("SELECT blob_key FROM scores")) throw new Error("unexpected first(): " + sql);
              const [season, players, key] = a;
              log.push(`survivor ${key}`);
              const r = rows.find((r) => r.season === season &&
                  r.players === players && r.platform_key === key);
              return r ? { blob_key: r.blob_key } : null;
            },
          };
        },
      };
      // An unbound statement (the revision bump) runs like a bound one.
      stmt.run = () => stmt.bind().run();
      return stmt;
    },
  };
}

function fake_r2() {
  const deleted = [];
  return { deleted, async delete(k) { deleted.push(k); } };
}

const hd = (score, run_id = "7") =>
  ({ season: "s1", run_id, score, generation: 3, duration_ms: 1000,
     format_version: 1, save_version: 23 });
const identity = { name: "A", platform: 2, verified: true };

// Both scenarios: an existing personal best (score 100, object K0) for run
// 7, then two uploads of run 7 — 300 under KA and 200 under KB — racing.
async function race(first, second) {
  const rows = [{ season: "s1", players: 1, run_id: "7", score: 100,
                  platform_key: "acct", blob_key: "s1/7-K0.nrp" }];
  const prev = { ...rows[0] };
  const log = [];
  const r2 = fake_r2();
  const s = new Session({}, { REPLAYS: r2 });
  const db = fake_db(rows, log);
  const [won_first, won_second] = await Promise.all([
    s.place_row(db, hd(first.score), 1, "acct", first.key, identity, prev),
    s.place_row(db, hd(second.score), 1, "acct", second.key, identity, prev),
  ]);
  return { rows, log, r2, won_first, won_second };
}

const KA = "s1/7-KA.nrp", KB = "s1/7-KB.nrp", K0 = "s1/7-K0.nrp";

// Higher score's upsert lands first.
{
  const t = await race({ score: 300, key: KA }, { score: 200, key: KB });
  check("race interleaves both upserts before either survivor check",
        t.log.join(",") === `upsert ${KA},upsert ${KB},survivor acct,survivor acct`,
        t.log.join(","));
  check("row keeps the winning score", t.rows[0].score === 300);
  check("row points at the WINNING upload's object", t.rows[0].blob_key === KA,
        t.rows[0].blob_key);
  check("winner reports placed", t.won_first === true);
  check("loser reports not-best (same run_id no longer fools it)",
        t.won_second === false);
  check("loser deleted only its own object", t.r2.deleted.includes(KB) &&
        !t.r2.deleted.includes(KA), JSON.stringify(t.r2.deleted));
  check("superseded best released by the winner", t.r2.deleted.includes(K0));
}

// Lower score's upsert lands first, then the higher one replaces it.
{
  const t = await race({ score: 200, key: KB }, { score: 300, key: KA });
  check("reverse order: row ends at the higher score", t.rows[0].score === 300);
  check("reverse order: row points at the final winner", t.rows[0].blob_key === KA);
  check("reverse order: the first, overtaken upload lost", t.won_first === false);
  check("reverse order: the overtaking upload won", t.won_second === true);
  check("reverse order: overtaken object deleted, winner's kept",
        t.r2.deleted.includes(KB) && !t.r2.deleted.includes(KA),
        JSON.stringify(t.r2.deleted));
  check("reverse order: superseded best released", t.r2.deleted.includes(K0));
}

// A plain same-run resubmission: the earlier upload's object is released.
{
  const rows = [{ season: "s1", players: 1, run_id: "7", score: 100,
                  platform_key: "acct", blob_key: K0 }];
  const r2 = fake_r2();
  const s = new Session({}, { REPLAYS: r2 });
  const won = await s.place_row(fake_db(rows, []), hd(150), 1, "acct", KA,
                                identity, { ...rows[0] });
  check("resubmit wins", won === true);
  check("resubmit releases the run's previous object", r2.deleted.includes(K0));
  check("resubmit row references the new object", rows[0].blob_key === KA);
}

// A worse resubmission loses and cleans up after itself only.
{
  const rows = [{ season: "s1", players: 1, run_id: "7", score: 100,
                  platform_key: "acct", blob_key: K0 }];
  const r2 = fake_r2();
  const s = new Session({}, { REPLAYS: r2 });
  const won = await s.place_row(fake_db(rows, []), hd(50), 1, "acct", KA,
                                identity, { ...rows[0] });
  check("worse resubmit loses", won === false);
  check("worse resubmit keeps the charting object",
        rows[0].blob_key === K0 && !r2.deleted.includes(K0));
  check("worse resubmit deletes its own object", r2.deleted.includes(KA));
}

// The other board's PRIMARY KEY abort propagates (finish_submit maps it to
// already-submitted and deletes only the aborted upload's own object).
{
  const rows = [{ season: "s1", players: 1, run_id: "7", score: 100,
                  platform_key: "acct", blob_key: K0 }];
  const r2 = fake_r2();
  const s = new Session({}, { REPLAYS: r2 });
  let threw = null;
  try {
    await s.place_row(fake_db(rows, []), hd(500), 2, "acct", KA, identity, null);
  } catch (e) { threw = e; }
  check("cross-board run_id collision aborts", threw !== null &&
        /constraint/i.test(threw.message));
  check("abort leaves the charting row's object alone",
        rows[0].blob_key === K0 && r2.deleted.length === 0);
}

console.log(failures ? `${failures} FAILED` : "submit_race_test: all passed");
process.exit(failures ? 1 : 0);
