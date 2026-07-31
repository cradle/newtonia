// Unit test for the admission gate (LEADERBOARD.md: no unattested
// replays): verify_identity must return null — reject — for every path
// that lacks a verifiable credential. The verifier INTERNALS (Valve,
// Google, Apple round-trips) are covered by signal/test/*_verify_test.mjs;
// this covers the board's gate around them. Pure node.
// Run: node test/identity_gate_test.mjs
import { verify_identity, rate_key, origin_allowed } from "../src/worker.js";

let failures = 0;
function eq(name, a, b) {
  const ok = a === b;
  console.log((ok ? "PASS " : "FAIL ") + name + (ok ? "" : `  (${a} !== ${b})`));
  if (!ok) failures++;
}

// FAKE_VERIFY (dev only) attests the claim and derives a per-name account.
{
  const v = await verify_identity({ FAKE_VERIFY: "1" }, 2, "ALICE", "");
  eq("fake verify attests", v && v.verified, true);
  eq("fake account per name", v.account, "fake:ALICE");
}
// No credential: rejected on every platform (empty and missing alike).
eq("steam no cred rejected", await verify_identity({}, 2, "X", ""), null);
eq("steam null cred rejected", await verify_identity({}, 2, "X", undefined), null);
// Platforms with no verifier can never be admitted.
eq("desktop rejected", await verify_identity({}, 1, "X", "cred"), null);
eq("web rejected", await verify_identity({}, 3, "X", "cred"), null);
eq("unknown platform rejected", await verify_identity({}, 99, "X", "cred"), null);
// Oversized credential rejected before any backend work.
eq("oversize cred rejected",
   await verify_identity({}, 2, "X", "a".repeat(8193)), null);
// Steam with a credential but no configured key: verifier declines.
eq("unconfigured steam rejected",
   await verify_identity({}, 2, "X", "deadbeef"), null);

// ---- rate_key (v6 collapse) ----
eq("v4 keyed whole", rate_key("203.0.113.9"), "v4:203.0.113.9");
eq("v6 collapsed to /64", rate_key("2001:db8:1:2:3:4:5:6"), "v6:2001:db8:1:2");
eq("v6 :: expanded", rate_key("2001:db8::1"), "v6:2001:db8:0:0");

// ---- origin gate (v1: native + local dev only) ----
eq("no origin allowed", origin_allowed({}, ""), true);
eq("localhost allowed", origin_allowed({}, "http://localhost:8788"), true);
eq("web origin refused", origin_allowed({}, "https://newtonia.metonymous.com"),
   false);
eq("secret override", origin_allowed({ ALLOWED_ORIGINS: ".example.com" },
   "https://play.example.com"), true);

process.exit(failures ? 1 : 0);
