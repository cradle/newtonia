// Unit test for the admission gate (LEADERBOARD.md: no unattested
// replays): verify_identity must return null — reject — for every path
// that lacks a verifiable credential. The verifier INTERNALS (Valve,
// Google, Apple round-trips) are covered by signal/test/*_verify_test.mjs;
// this covers the board's gate around them. Pure node.
// Run: node test/identity_gate_test.mjs
import { verify_identity, rate_key, origin_allowed, is_dev_host, log_str }
  from "../src/worker.js";

let failures = 0;
function eq(name, a, b) {
  const ok = a === b;
  console.log((ok ? "PASS " : "FAIL ") + name + (ok ? "" : `  (${a} !== ${b})`));
  if (!ok) failures++;
}

// FAKE_VERIFY (dev only) attests the claim and derives a per-name account
// — but still requires a non-empty credential, like every real backend
// (the client's empty-at-submit retry case depends on this rejection).
{
  const v = await verify_identity({ FAKE_VERIFY: "1" }, 2, "ALICE", "tick", true);
  eq("fake verify attests", v && v.verified, true);
  eq("fake account per name", v.account, "fake:ALICE");
  eq("fake verify still needs a cred",
     await verify_identity({ FAKE_VERIFY: "1" }, 2, "ALICE", "", true), null);
}

// ...but ONLY on a dev host (LEADERBOARD.md S6). The variable alone must not
// be able to turn a deployed worker into an open board: without the second
// condition, one stray dashboard var attests every claim silently.
{
  eq("fake verify inert off a dev host",
     await verify_identity({ FAKE_VERIFY: "1" }, 2, "ALICE", "tick", false),
     null);
  eq("fake verify inert when dev is simply absent",
     await verify_identity({ FAKE_VERIFY: "1" }, 2, "ALICE", "tick"), null);
}

// ---- is_dev_host: loopback only ----
eq("localhost is dev", is_dev_host("localhost"), true);
eq("127.0.0.1 is dev", is_dev_host("127.0.0.1"), true);
eq("v6 loopback is dev", is_dev_host("::1"), true);
eq("sub.localhost is dev", is_dev_host("api.localhost"), true);
eq("production host is not dev",
   is_dev_host("newtonia-board.gfmcc.workers.dev"), false);
eq("beta host is not dev",
   is_dev_host("newtonia-board-beta.gfmcc.workers.dev"), false);
// A hostname merely CONTAINING the loopback name must not pass.
eq("lookalike host is not dev", is_dev_host("localhost.evil.com"), false);
eq("suffix lookalike is not dev", is_dev_host("notlocalhost"), false);
eq("127.0.0.1 lookalike is not dev", is_dev_host("127.0.0.1.evil.com"), false);
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

// ---- log_str: client text that reaches a log line (LEADERBOARD.md S6) ----
// A newline is the whole point: without stripping it, a submitter's field
// forges entire log entries.
eq("newline stripped", log_str("2\nplaced: season=s1 score=999999"),
   "2placed: season=s1 score=999999");
eq("control bytes stripped", log_str("a\x00\x1b[31mb"), "a[31mb");
eq("non-ascii stripped", log_str("caf\u00e9"), "caf");
eq("length capped", log_str("x".repeat(500)).length, 64);
eq("non-string coerced", log_str(7), "7");
eq("object coerced safely", log_str({}), "[object Object]");

process.exit(failures ? 1 : 0);
