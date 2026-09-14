// Submission storage-ownership test (Workers review 2026-09-08, F1/F2;
// submit_race_test.mjs covers place_row alone against a fake — this one):
// drives the real Session.finish_submit against a real SQLite
// (d1_sqlite.mjs) and an in-memory R2 fake whose hooks let two sessions be
// interleaved DETERMINISTICALLY at chosen statements. The properties:
//
//   F1  Every upload gets its own immutable object key, the row names the
//       key it was written with, and the surviving row's score and blob
//       always agree — for two versions of ONE run racing under one
//       account (the higher score must win the slot AND the object), and
//       for two accounts colliding on a run_id (the loser's constraint
//       abort must delete only its own object).
//   F2  Only a DEFINITE non-commit deletes the uploaded object. A failure
//       AFTER the upsert committed (the survivor read) leaves the object:
//       the committed row points at it, and the client cannot resubmit.
//   plus the ordinary same-account improvement paths: a new run and a
//       resumed same-run improvement each replace the previous object.
//
// Run: node test/submit_storage_test.mjs
import { Session, ensure_schema } from "../src/worker.js";
import { d1_sqlite, clear_rows, sql_get, sql_all } from "./d1_sqlite.mjs";
import { build_nrp } from "./nrp_fixture.mjs";

let failures = 0;
function check(name, cond, detail) {
  console.log((cond ? "PASS " : "FAIL ") + name +
              (cond || detail === undefined ? "" : `  (${detail})`));
  if (!cond) failures++;
}

await ensure_schema(d1_sqlite());

function deferred() {
  let resolve;
  const promise = new Promise((r) => { resolve = r; });
  return { promise, resolve };
}

// One bucket shared by every session, with per-session hooks.
function bucket() {
  return { objects: new Map(), deleted: [] };
}
function r2_view(store, hooks = {}) {
  return {
    async put(key, body) {
      store.objects.set(key, new Uint8Array(body));
      if (hooks.after_put) await hooks.after_put(key);
    },
    async delete(k) {
      for (const key of Array.isArray(k) ? k : [k]) {
        store.objects.delete(key);
        store.deleted.push(key);
      }
    },
    async get(key) {
      const b = store.objects.get(key);
      return b ? { arrayBuffer: async () => b.buffer.slice(0) } : null;
    },
    async list() { return { objects: [], truncated: false }; },
  };
}

function fake_ws() {
  return { sent: [], send(s) { this.sent.push(JSON.parse(s)); }, close() {} };
}

function identity(name) {
  return { platform: 2, name, verified: true, account: `fake:${name}` };
}

function session(store, db_hooks = {}, r2_hooks = {}) {
  const s = new Session({}, { DB: d1_sqlite(db_hooks),
                              REPLAYS: r2_view(store, r2_hooks) });
  s.hydrated = true;
  s.dev = true;
  return s;
}

// The score a stored object's header carries (fixture layout, LE u32 @52).
function blob_score(bytes) {
  return new DataView(bytes.buffer, bytes.byteOffset).getUint32(52, true);
}

const SEASON = "s1";
const RUN = 777n;

// 1. F1 — the same run, one account, two uploads racing: both pass the
// pre-checks before either row lands; the LOWER score's object is written
// LAST. Under the old shared key the row said 300 and the object said 200,
// and both sessions answered placed.
{
  clear_rows();
  const store = bucket();
  const g0 = deferred(), g1 = deferred(), g2 = deferred();
  const A = session(store,
      { before: async (sql) => { if (sql.startsWith("INSERT")) await g1.promise; },
        after:  async (sql) => { if (sql.startsWith("INSERT")) g2.resolve(); } },
      { after_put: async () => g0.resolve() });
  const B = session(store,
      { before: async (sql) => {
          if (sql.includes("SELECT score, platform_key")) await g0.promise;
          if (sql.startsWith("INSERT")) await g2.promise;
        } },
      { after_put: async () => g1.resolve() });
  const wsA = fake_ws(), wsB = fake_ws();
  const hi = build_nrp({ game_version: SEASON, run_id: RUN, score: 300 });
  const lo = build_nrp({ game_version: SEASON, run_id: RUN, score: 200 });
  await Promise.all([
    A.finish_submit(wsA, hi, identity("ALICE")),
    B.finish_submit(wsB, lo, identity("ALICE")),
  ]);
  const row = sql_get(`SELECT score, blob_key FROM scores WHERE run_id = '777'`);
  check("same-run race: the higher score holds the row", row && row.score === 300,
        JSON.stringify(row));
  const obj = row && store.objects.get(row.blob_key);
  check("same-run race: the row's object IS the higher score's blob",
        !!obj && blob_score(obj) === 300, obj && `${blob_score(obj)}`);
  check("same-run race: exactly one object survives", store.objects.size === 1,
        `${store.objects.size}`);
  check("same-run race: higher score answered placed",
        wsA.sent.some((f) => f.t === "placed"), JSON.stringify(wsA.sent));
  check("same-run race: lower score was NOT told placed",
        !wsB.sent.some((f) => f.t === "placed") &&
        wsB.sent.some((f) => f.t === "err" && f.reason === "not-best"),
        JSON.stringify(wsB.sent));
}

// 2. F1 — two ACCOUNTS colliding on one run_id (a copied file), racing
// past the pre-checks: the loser's INSERT aborts on the (season, run_id)
// primary key. Under the shared key its cleanup deleted the WINNER's
// object; now it deletes only its own and answers already-submitted.
{
  clear_rows();
  const store = bucket();
  const g0 = deferred(), g1 = deferred(), g2 = deferred();
  const A = session(store,
      { before: async (sql) => { if (sql.startsWith("INSERT")) await g1.promise; },
        after:  async (sql) => { if (sql.startsWith("INSERT")) g2.resolve(); } },
      { after_put: async () => g0.resolve() });
  const B = session(store,
      { before: async (sql) => {
          if (sql.includes("SELECT score, platform_key")) await g0.promise;
          if (sql.startsWith("INSERT")) await g2.promise;
        } },
      { after_put: async () => g1.resolve() });
  const wsA = fake_ws(), wsB = fake_ws();
  const a = build_nrp({ game_version: SEASON, run_id: RUN, score: 300 });
  const b = build_nrp({ game_version: SEASON, run_id: RUN, score: 200 });
  await Promise.all([
    A.finish_submit(wsA, a, identity("ALICE")),
    B.finish_submit(wsB, b, identity("BOB")),
  ]);
  const row = sql_get(`SELECT score, blob_key, name FROM scores WHERE run_id = '777'`);
  check("cross-account race: the first committer holds the row",
        row && row.name === "ALICE" && row.score === 300, JSON.stringify(row));
  check("cross-account race: the winner's object survives",
        !!row && store.objects.has(row.blob_key), JSON.stringify([...store.objects.keys()]));
  check("cross-account race: the loser's object is deleted",
        store.objects.size === 1 && store.deleted.length === 1,
        JSON.stringify({ objects: [...store.objects.keys()], deleted: store.deleted }));
  check("cross-account race: the loser is told already-submitted",
        wsB.sent.some((f) => f.t === "err" && f.reason === "already-submitted"),
        JSON.stringify(wsB.sent));
  check("cross-account race: the winner is told placed",
        wsA.sent.some((f) => f.t === "placed"), JSON.stringify(wsA.sent));
}

// 3. F2 — the survivor read fails AFTER the upsert committed. The object
// must stay: the row points at it, and a retry meets already-submitted.
{
  clear_rows();
  const store = bucket();
  const S = session(store, {
    before: async (sql) => {
      if (sql.includes("SELECT blob_key FROM scores") &&
          sql.includes("platform_key = ?3"))
        throw new Error("D1 unavailable");
    },
  });
  const ws = fake_ws();
  const blob = build_nrp({ game_version: SEASON, run_id: RUN, score: 500 });
  let threw = false;
  try { await S.finish_submit(ws, blob, identity("ALICE")); }
  catch (e) { threw = true; }
  check("post-commit read failure surfaces as an error", threw);
  const row = sql_get(`SELECT score, blob_key FROM scores WHERE run_id = '777'`);
  check("post-commit read failure: the score committed",
        row && row.score === 500, JSON.stringify(row));
  check("post-commit read failure: the committed row's object is NOT deleted",
        !!row && store.objects.has(row.blob_key) && store.deleted.length === 0,
        JSON.stringify({ row, deleted: store.deleted }));
}

// 4. F2 — a failure BEFORE anything could commit (the upsert itself, not
// a constraint) also leaves the object: the outcome is uncertain from the
// worker's side, and the orphan sweep reclaims it if no row claims it.
{
  clear_rows();
  const store = bucket();
  const S = session(store, {
    before: async (sql) => { if (sql.startsWith("INSERT")) throw new Error("D1 timeout"); },
  });
  let threw = false;
  try { await S.finish_submit(fake_ws(), build_nrp({ game_version: SEASON,
        run_id: RUN, score: 500 }), identity("ALICE")); }
  catch (e) { threw = true; }
  check("uncertain upsert failure surfaces", threw);
  check("uncertain upsert failure: object left for the orphan sweep",
        store.objects.size === 1 && store.deleted.length === 0);
}

// 5. Ordinary improvements: a NEW run replaces the account's previous
// object, and a RESUMED same-run improvement replaces the earlier upload
// of that run (each upload has its own key now, so nothing overwrites in
// place — the winner must delete the superseded object explicitly).
{
  clear_rows();
  const store = bucket();
  const S = session(store);
  const ws = fake_ws();
  await S.finish_submit(ws, build_nrp({ game_version: SEASON, run_id: 1n,
                                        score: 100 }), identity("ALICE"));
  const k1 = sql_get(`SELECT blob_key FROM scores WHERE run_id = '1'`).blob_key;
  await S.finish_submit(ws, build_nrp({ game_version: SEASON, run_id: 1n,
                                        score: 150 }), identity("ALICE"));
  const k1b = sql_get(`SELECT blob_key, score FROM scores WHERE run_id = '1'`);
  check("same-run improvement: new key, new score",
        k1b.score === 150 && k1b.blob_key !== k1, JSON.stringify(k1b));
  check("same-run improvement: earlier upload's object deleted",
        store.deleted.includes(k1) && store.objects.has(k1b.blob_key) &&
        store.objects.size === 1);
  await S.finish_submit(ws, build_nrp({ game_version: SEASON, run_id: 2n,
                                        score: 200 }), identity("ALICE"));
  check("new-run improvement: previous run's object deleted",
        store.deleted.includes(k1b.blob_key) && store.objects.size === 1);
  check("new-run improvement: one row per account",
        sql_all(`SELECT run_id FROM scores`).length === 1);
  await S.finish_submit(ws, build_nrp({ game_version: SEASON, run_id: 2n,
                                        score: 120 }), identity("ALICE"));
  check("same-run regression refused as already-submitted",
        ws.sent.at(-1).t === "err" && ws.sent.at(-1).reason === "already-submitted",
        JSON.stringify(ws.sent.at(-1)));
  check("keys carry season/run_id and are unique per upload",
        /^s1\/2-[0-9a-f]+\.nrp$/.test([...store.objects.keys()][0]),
        [...store.objects.keys()][0]);
}

console.log(failures ? `${failures} FAILURE(S)` : "ALL PASS");
process.exit(failures ? 1 : 0);
