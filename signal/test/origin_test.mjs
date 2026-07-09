// Unit test for origin_allowed: the shipped web origins and the itch.io
// game-iframe CDN hosts are permitted; a missing Origin (native client) is
// permitted; an unrelated site is refused so it can't harvest TURN creds
// from a visitor's browser. Run: node test/origin_test.mjs
import { origin_allowed } from "../src/worker.js";

let failures = 0;
function ok(name, v) {
  const pass = v === true;
  console.log((pass ? "PASS " : "FAIL ") + name);
  if (!pass) failures++;
}
function no(name, v) {
  const pass = v === false;
  console.log((pass ? "PASS " : "FAIL ") + name);
  if (!pass) failures++;
}

const env = {}; // no ALLOWED_ORIGINS override -> built-in defaults

// Allowed: shipped browser origins (Origin has no path, so /play/ etc. drop).
ok("metonymous site",   origin_allowed(env, "https://newtonia.metonymous.com"));
ok("itch project page", origin_allowed(env, "https://metonymous.itch.io"));
ok("itch iframe CDN",   origin_allowed(env, "https://v6p9d9t4.ssl.hwcdn.net"));
ok("itch.zone iframe",  origin_allowed(env, "https://html-classic.itch.zone"));
ok("localhost dev",     origin_allowed(env, "http://localhost:8080"));

// Allowed: native / non-browser caller sends no Origin header.
ok("no origin (native)", origin_allowed(env, null));

// Refused: unrelated sites and near-miss look-alikes.
no("unrelated site",     origin_allowed(env, "https://evil.example"));
no("http not https site",origin_allowed(env, "http://newtonia.metonymous.com"));
no("suffix spoof",       origin_allowed(env, "https://newtonia.metonymous.com.evil.example"));
no("itch host spoof",    origin_allowed(env, "https://itch.zone.evil.example"));
no("malformed origin",   origin_allowed(env, "not a url"));

// Override via secret: only the listed origin passes.
const envOverride = { ALLOWED_ORIGINS: "https://only.example, .lan.internal" };
ok("override exact",     origin_allowed(envOverride, "https://only.example"));
ok("override subdomain", origin_allowed(envOverride, "https://box.lan.internal"));
no("override excludes default",
   origin_allowed(envOverride, "https://newtonia.metonymous.com"));

console.log(failures ? `\n${failures} FAILURES` : "\nORIGIN-TEST-OK");
process.exit(failures ? 1 : 0);
