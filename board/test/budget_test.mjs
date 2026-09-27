// Unit test for the two budget properties S5 was about (LEADERBOARD.md):
//
//   1. An UNANSWERABLE per-IP limiter refuses a submit and allows a read.
//      The per-IP window is the only durable bound on submissions, so
//      failing open there lifts the ceiling entirely during an outage;
//      reads stay open because locking the board out over a hiccup is the
//      worse failure.
//   2. Per-connection budgets survive the DO being reconstructed. They live
//      in memory on a hibernation-enabled DO, so they ride the socket
//      attachment rather than the instance.
//
// Pure node, no wrangler. Run: node test/budget_test.mjs
import { within_limit, Session } from "../src/worker.js";

let failures = 0;
function eq(name, a, b) {
  const ok = a === b;
  console.log((ok ? "PASS " : "FAIL ") + name + (ok ? "" : `  (${a} !== ${b})`));
  if (!ok) failures++;
}

// A LIMITS binding whose DO answers, or breaks, on demand.
function limiter_env(answer) {
  return {
    LIMITS: {
      idFromName: (n) => n,
      get: () => ({
        fetch: async () => {
          if (answer === "throw") throw new Error("limiter down");
          if (answer === "garbage") return { json: async () => { throw new Error("bad json"); } };
          return { json: async () => ({ allowed: answer }) };
        },
      }),
    },
  };
}

// ---- 1. limiter answers are honoured either way ----
eq("allowed passes", await within_limit(limiter_env(true), "ip", "submit"), true);
eq("refused blocks", await within_limit(limiter_env(false), "ip", "submit"), false);

// ---- ...and an unanswerable limiter fails CLOSED for submit only ----
for (const broken of ["throw", "garbage"]) {
  eq(`submit fails closed (${broken})`,
     await within_limit(limiter_env(broken), "ip", "submit"), false);
  eq(`query fails open (${broken})`,
     await within_limit(limiter_env(broken), "ip", "query"), true);
  eq(`fetch fails open (${broken})`,
     await within_limit(limiter_env(broken), "ip", "fetch"), true);
  eq(`conn fails open (${broken})`,
     await within_limit(limiter_env(broken), "ip", "conn"), true);
}

// ---- 2. budgets ride the socket, not the instance ----
// A fake WebSocket with just the attachment API hibernation uses.
function fake_ws() {
  let attached = null;
  return {
    serializeAttachment(v) { attached = JSON.parse(JSON.stringify(v)); },
    deserializeAttachment() { return attached; },
  };
}

{
  const ws = fake_ws();
  const live = new Session({}, {});
  live.hydrated = true;          // as Session.fetch marks the origin instance
  live.ip = "v4:203.0.113.9";
  live.dev = true;
  live.queries = 118;
  live.submits = 2;
  live.fetches = 5;
  live.forced_reject_once_ = true;
  live.persist(ws);

  // Hibernation: the class is reconstructed with everything at defaults and
  // the next message arrives on the same socket.
  const woken = new Session({}, {});
  eq("fresh instance starts empty", woken.queries, 0);
  woken.hydrate(ws);
  eq("queries restored", woken.queries, 118);
  eq("submits restored", woken.submits, 2);
  eq("fetches restored", woken.fetches, 5);
  eq("ip restored", woken.ip, "v4:203.0.113.9");
  eq("dev restored", woken.dev, true);
  eq("forced-reject flag restored", woken.forced_reject_once_, true);

  // Budgets must still BITE after waking: one more query is over the cap.
  eq("restored budget is at its limit", woken.queries + 1 > 120, false);
  woken.queries = 120;
  eq("restored budget refuses past the cap", woken.queries + 1 > 120, true);
}

{
  // Hydrating twice must not clobber counters the live instance has moved
  // on from — the in-memory object is authoritative once hydrated, which is
  // what keeps interleaved handlers race-free.
  const ws = fake_ws();
  const s = new Session({}, {});
  s.hydrated = true;
  s.queries = 4;
  s.persist(ws);
  const woken = new Session({}, {});
  woken.hydrate(ws);
  woken.queries = 9;             // more messages since waking
  woken.hydrate(ws);             // a second delivery must be a no-op
  eq("second hydrate is a no-op", woken.queries, 9);
}

{
  // A PARTIAL attachment — a field this code did not write — must not
  // restore `undefined`: ++undefined is NaN, and NaN compares false against
  // every cap, so the budget would silently stop existing. A guard may not
  // fail open because a field went missing.
  const ws = fake_ws();
  ws.serializeAttachment({ ip: "v4:203.0.113.9", dev: false });  // no counters
  const s = new Session({}, {});
  s.hydrate(ws);
  eq("missing counter restores 0, not undefined", s.queries, 0);
  s.queries++;
  eq("counter still counts after a partial attachment", s.queries, 1);
  eq("budget still bites", (s.queries = 121) > 120, true);
  // Junk in the fields is coerced too, never trusted through.
  const s2 = new Session({}, {});
  s2.hydrate({ deserializeAttachment: () => ({
    ip: 42, dev: "yes", queries: "lots", submits: null, fetches: {},
    forced: "true" }) });
  eq("non-numeric counter coerced to 0", s2.queries, 0);
  eq("non-string ip left at the default", s2.ip, "local");
  eq("non-boolean dev is false", s2.dev, false);
  eq("non-boolean forced flag is false", s2.forced_reject_once_, false);
}

{
  // A socket with no attachment (or one that throws) must leave defaults
  // rather than crash the message handler.
  const s = new Session({}, {});
  s.hydrate({ deserializeAttachment: () => null });
  eq("no attachment leaves defaults", s.queries, 0);
  const s2 = new Session({}, {});
  s2.hydrate({ deserializeAttachment: () => { throw new Error("nope"); } });
  eq("throwing attachment leaves defaults", s2.queries, 0);
  s2.persist({ serializeAttachment: () => { throw new Error("nope"); } });
  eq("throwing persist is swallowed", s2.queries, 0);
}

// ---- 3. platform verifier calls ride their own fail-closed budget ----
for (const broken of ["throw", "garbage"])
  eq(`verify fails closed (${broken})`,
     await within_limit(limiter_env(broken), "ip", "verify"), false);

{
  // A credentialed qualify over the per-IP verify budget must not reach the
  // platform verifier (it is answered anonymously), and a submit with no
  // qualify identity to stand on is refused the same way. Real verifier
  // (dev off, Android platform), so any call would hit the stubbed fetch.
  let platform_calls = 0;
  const real_fetch = globalThis.fetch;
  globalThis.fetch = async () => { platform_calls++; throw new Error("no"); };
  const stmt = { bind() { return stmt; }, first: async () => null,
                 all: async () => ({ results: [] }), run: async () => ({}) };
  const env = {
    DB: { prepare: () => stmt, batch: async () => [] },
    PLAY_GAMES_OAUTH_CLIENT_ID: "id", PLAY_GAMES_OAUTH_CLIENT_SECRET: "sec",
    LIMITS: { idFromName: (n) => n, get: () => ({
      fetch: async (url) => ({ json: async () => ({
        allowed: !String(url).includes("action=verify") }) }) }) },
  };
  const sent = [];
  const ws = { send: (t) => sent.push(JSON.parse(t)),
               serializeAttachment() {}, deserializeAttachment: () => null,
               close() {} };
  const s = new Session({}, env);
  s.hydrated = true;
  await s.on_json(ws, JSON.stringify({ t: "qualify", season: "s1",
      players: 1, score: 500, platform: 5, name: "P", cred: "code" }));
  eq("over-budget qualify still answers", sent[0] && sent[0].t, "qualify");
  eq("over-budget qualify is anonymous", sent[0] && sent[0].own_best, false);
  eq("over-budget qualify skips the verifier", platform_calls, 0);
  eq("over-budget qualify holds no identity", s.qualify_identity_, null);
  await s.on_json(ws, JSON.stringify({ t: "submit", size: 5000,
      platform: 5, name: "P", cred: "code" }));
  eq("over-budget submit refused", sent[1] && sent[1].reason, "rate-limited");
  eq("over-budget submit skips the verifier", platform_calls, 0);
  globalThis.fetch = real_fetch;
}

{
  // The qualify's verified identity rides the socket like the counters, so
  // a hibernation between qualify and submit can't strand the upload.
  const ws = fake_ws();
  const s = new Session({}, {});
  s.hydrated = true;
  s.qualify_verified_ = true;
  s.qualify_identity_ = { platform: 5, name: "P", verified: true,
                          account: "pg:1" };
  s.persist(ws);
  const woken = new Session({}, {});
  woken.hydrate(ws);
  eq("qualify-verified flag restored", woken.qualify_verified_, true);
  eq("qualify identity restored",
     woken.qualify_identity_ && woken.qualify_identity_.account, "pg:1");
  const junk = new Session({}, {});
  junk.hydrate({ deserializeAttachment: () => ({ qv: "yes", qid: { a: 1 } }) });
  eq("junk qualify identity refused", junk.qualify_identity_, null);
  eq("non-boolean qualify flag is false", junk.qualify_verified_, false);
}

console.log(failures ? `${failures} FAILURE(S)` : "ALL PASS");
process.exit(failures ? 1 : 0);
