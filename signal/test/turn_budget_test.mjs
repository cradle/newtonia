// Unit test for the TURN monthly egress budget: the mint gate must trip
// when the account's measured month-to-date egress crosses the budget, stay
// open when analytics is unconfigured/unreadable (cost cap, not auth gate),
// hold the last verdict across API failures, and query the current UTC
// month for our key.
// Run: node test/turn_budget_test.mjs
import {
  turn_budget_gb,
  turn_egress_month_bytes,
  turn_budget_ok,
  turn_budget_cache_reset,
} from "../src/worker.js";

let failures = 0;
function eq(name, a, b) {
  const ok = a === b;
  console.log((ok ? "PASS " : "FAIL ") + name + (ok ? "" : `  (${a} !== ${b})`));
  if (!ok) failures++;
}

const ENV = {
  CF_ACCOUNT_ID: "acct123",
  CF_ANALYTICS_TOKEN: "tok",
  TURN_KEY_ID: "key456",
};

function graphql_response(egressBytesList) {
  return {
    ok: true,
    json: async () => ({
      data: { viewer: { accounts: [{
        callsTurnUsageAdaptiveGroups:
          egressBytesList.map((b) => ({ sum: { egressBytes: b } })),
      }] } },
    }),
  };
}

// ---- turn_budget_gb ----
eq("default budget is 900 GB", turn_budget_gb({}), 900);
eq("TURN_BUDGET_GB overrides", turn_budget_gb({ TURN_BUDGET_GB: "50" }), 50);
eq("garbage override falls back", turn_budget_gb({ TURN_BUDGET_GB: "x" }), 900);

// ---- turn_egress_month_bytes ----
{
  let captured = null;
  const fetcher = async (url, opts) => { captured = { url, opts }; return graphql_response([1e9, 2e9]); };
  const now = new Date("2026-07-18T10:00:00Z");
  const bytes = await turn_egress_month_bytes(ENV, fetcher, now);
  eq("sums all groups", bytes, 3e9);
  eq("queries the graphql endpoint",
     captured.url, "https://api.cloudflare.com/client/v4/graphql");
  const query = JSON.parse(captured.opts.body).query;
  eq("filters our key", query.includes('keyId: "key456"'), true);
  eq("from the first of the UTC month", query.includes('date_geq: "2026-07-01"'), true);
  eq("to today", query.includes('date_leq: "2026-07-18"'), true);
  eq("bearer token sent",
     captured.opts.headers.Authorization, "Bearer tok");
}
eq("unconfigured env reads null",
   await turn_egress_month_bytes({}, async () => graphql_response([0])), null);
eq("http error reads null",
   await turn_egress_month_bytes(ENV, async () => ({ ok: false })), null);
eq("network throw reads null",
   await turn_egress_month_bytes(ENV, async () => { throw new Error("net"); }), null);

// ---- turn_budget_ok ----
const T0 = 1_000_000_000_000;
{
  turn_budget_cache_reset();
  const under = async () => graphql_response([100e9]); // 100 GB
  eq("under budget mints", await turn_budget_ok(ENV, under, T0), true);
  // Within the recheck window the cache answers — even a would-be-over
  // fetcher isn't consulted.
  const over = async () => graphql_response([950e9]);
  eq("cached verdict holds inside the window",
     await turn_budget_ok(ENV, over, T0 + 60 * 1000), true);
  // Past the window the fresh over-budget reading trips the gate.
  eq("over budget trips after recheck",
     await turn_budget_ok(ENV, over, T0 + 16 * 60 * 1000), false);
  // An API failure keeps the tripped verdict (no fail-open flapping).
  const broken = async () => ({ ok: false });
  eq("api failure keeps last (tripped) verdict",
     await turn_budget_ok(ENV, broken, T0 + 32 * 60 * 1000), false);
}
{
  turn_budget_cache_reset();
  // Never measured (unconfigured): the cost cap stays open.
  eq("unmeasured stays open", await turn_budget_ok({}, async () => { throw new Error(); }, T0), true);
}
{
  turn_budget_cache_reset();
  // Custom budget respected.
  const env = { ...ENV, TURN_BUDGET_GB: "50" };
  const at60 = async () => graphql_response([60e9]);
  eq("custom 50 GB budget trips at 60 GB", await turn_budget_ok(env, at60, T0), false);
}

process.exit(failures ? 1 : 0);
