// Unit test for rate_key: IPv6 addresses in the same /64 must collapse to
// one key (so rotating within a /64 can't bypass the per-IP limit), while
// distinct /64s and distinct IPv4 addresses stay separate.
// Run: node test/rate_key_test.mjs
import { rate_key } from "../src/worker.js";

let failures = 0;
function eq(name, a, b) {
  const ok = a === b;
  console.log((ok ? "PASS " : "FAIL ") + name + (ok ? "" : `  (${a} !== ${b})`));
  if (!ok) failures++;
}
function ne(name, a, b) {
  const ok = a !== b;
  console.log((ok ? "PASS " : "FAIL ") + name + (ok ? "" : `  (${a} === ${b})`));
  if (!ok) failures++;
}

// Same /64, different host bits -> same key.
eq("v6 same /64 collapses",
   rate_key("2001:db8:abcd:1234::1"),
   rate_key("2001:db8:abcd:1234:ffff:ffff:ffff:ffff"));
eq("v6 same /64 (expanded vs ::)",
   rate_key("2001:db8:abcd:1234:0:0:0:1"),
   rate_key("2001:db8:abcd:1234::5"));

// Different /64 -> different key.
ne("v6 different /64 stays separate",
   rate_key("2001:db8:abcd:1234::1"),
   rate_key("2001:db8:abcd:9999::1"));

// IPv4 keys whole and never collides with v6.
ne("v4 addresses distinct",  rate_key("203.0.113.7"), rate_key("203.0.113.8"));
ne("v4 and v6 never collide", rate_key("203.0.113.7"), rate_key("2001:db8::7"));

// Leading-:: and full forms of the same address agree.
eq("v6 leading :: normalizes",
   rate_key("::1"),
   rate_key("0:0:0:0:0:0:0:1"));

console.log(failures ? `\n${failures} FAILURES` : "\nRATE-KEY-TEST-OK");
process.exit(failures ? 1 : 0);
