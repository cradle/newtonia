// Unit test for the Game Center identity verifier (NETPLAY.md V3): the worker
// fetches Apple's public-key cert and cryptographically verifies the client's
// identity-verification signature, PROVING the account (account-only — no alias
// is ever derived, Apple exposes none server-side). Uses a locally generated
// RSA keypair to mint a genuine signature and a synthetic X.509 cert wrapping
// that key, mocking only the cert fetch — no Apple/network involved.
// Run: node test/game_center_verify_test.mjs
import { generateKeyPairSync, createSign } from "node:crypto";
import {
  verifyGameCenterCred,
  extractSPKI,
  is_apple_host,
  BUNDLE_ID,
  MAX_CRED_LEN,
  MAX_TIMESTAMP_SKEW_MS,
} from "../src/game_center_verify.js";

let failures = 0;
function eq(name, a, b) {
  const ok = a === b;
  console.log((ok ? "PASS " : "FAIL ") + name + (ok ? "" : `  (${a} !== ${b})`));
  if (!ok) failures++;
}
function deep(name, a, b) { eq(name, JSON.stringify(a), JSON.stringify(b)); }

// ---- DER builders for a synthetic X.509 cert wrapping our public key --------
function derLen(n) {
  if (n < 128) return Buffer.from([n]);
  const bytes = [];
  while (n > 0) { bytes.unshift(n & 0xff); n = Math.floor(n / 256); }
  return Buffer.from([0x80 | bytes.length, ...bytes]);
}
function tlv(tag, value) {
  return Buffer.concat([Buffer.from([tag]), derLen(value.length), value]);
}
function seq(...parts) { return tlv(0x30, Buffer.concat(parts)); }

// Wrap an SPKI (the exported public key DER) in a minimal Certificate whose
// TBSCertificate has NO version field, so subjectPublicKeyInfo is the 6th child
// (index 5) — exactly what extractSPKI walks to.
function fakeCert(spkiDer) {
  const serial = tlv(0x02, Buffer.from([0x01]));
  const empty = seq(Buffer.alloc(0));            // stand-in AlgId/Name/Validity
  const tbs = seq(serial, empty, empty, empty, empty, spkiDer);
  const sigVal = tlv(0x03, Buffer.from([0x00])); // BIT STRING, 0 unused bits
  return seq(tbs, empty, sigVal);
}

function uint64BE(n) {
  const b = Buffer.alloc(8);
  b.writeBigUInt64BE(BigInt(n));
  return b;
}

// ---- fixtures --------------------------------------------------------------
const { publicKey, privateKey } = generateKeyPairSync("rsa", { modulusLength: 2048 });
const SPKI = publicKey.export({ type: "spki", format: "der" });
const CERT = fakeCert(SPKI);
const PK_URL = "https://static.gc.apple.com/public-key/gc-prod-9.cer";
const GPID = "G:1122334455";
const TPID = "T:9988776655";
const SALT = Buffer.from("c2FsdHNhbHRzYWx0", "utf8"); // arbitrary bytes
const TS = 1750000000000; // ms; test passes `now = TS` so it's always fresh

// Sign identifier|bundleId|uint64BE(ts)|salt with `alg` (Apple's RSA cert).
function sign(identifier, bundleId, ts, salt, alg = "RSA-SHA256") {
  const payload = Buffer.concat([
    Buffer.from(identifier, "utf8"),
    Buffer.from(bundleId, "utf8"),
    uint64BE(ts),
    salt,
  ]);
  return createSign(alg).update(payload).sign(privateKey);
}

// Build a cred JSON around a signature. Overrides let each test tweak a field.
function cred(over = {}) {
  const o = {
    pk: PK_URL, salt: SALT.toString("base64"), ts: TS,
    gpid: GPID, tpid: TPID, bid: BUNDLE_ID, ...over,
  };
  if (o.sig === undefined)
    o.sig = sign(o._signId ?? GPID, o.bid, o.ts, SALT, o._alg ?? "RSA-SHA256")
              .toString("base64");
  delete o._signId; delete o._alg;
  return JSON.stringify(o);
}

// A cert-serving mock; records the URLs it was asked for.
function apple(certBuf = CERT) {
  const urls = [];
  const fetcher = async (url) => {
    urls.push(url);
    return { ok: true, arrayBuffer: async () => Uint8Array.from(certBuf).buffer };
  };
  return { fetcher, urls };
}
const ENV = {};

// ---- is_apple_host ----
eq("apple.com accepted", is_apple_host("apple.com"), true);
eq("static.gc.apple.com accepted", is_apple_host("static.gc.apple.com"), true);
eq("evil.com rejected", is_apple_host("evil.com"), false);
eq("notapple.com rejected", is_apple_host("notapple.com"), false);
eq("apple.com.evil.com rejected", is_apple_host("apple.com.evil.com"), false);

// ---- extractSPKI round-trips our public key ----
{
  const got = Buffer.from(extractSPKI(Uint8Array.from(CERT)));
  eq("extractSPKI returns the embedded SPKI", got.equals(Buffer.from(SPKI)), true);
}

// ---- happy path: gpid-signed, SHA-256 ----
{
  const { fetcher, urls } = apple();
  const v = await verifyGameCenterCred(ENV, cred(), fetcher, TS);
  deep("valid gpid signature -> account proven + winning combo reported", v,
       { identifier: GPID, idKind: "gamePlayerID", hash: "SHA-256" });
  eq("fetched the apple cert url", urls[0], PK_URL);
}

// ---- account attested, but the caller (worker) uses name:"" — verifier never
//      returns an alias (none is derivable) ----
{
  const { fetcher } = apple();
  const v = await verifyGameCenterCred(ENV, cred(), fetcher, TS);
  eq("verifier never yields a name field", v.name, undefined);
}

// ---- signed with teamPlayerID instead: the tpid branch verifies ----
{
  const { fetcher } = apple();
  const c = cred({ _signId: TPID });
  const v = await verifyGameCenterCred(ENV, c, fetcher, TS);
  deep("tpid signature -> proven via teamPlayerID", v,
       { identifier: TPID, idKind: "teamPlayerID", hash: "SHA-256" });
}

// ---- digest fallback: a SHA-1 signature still verifies ----
{
  const { fetcher } = apple();
  const c = cred({ _alg: "RSA-SHA1" });
  const v = await verifyGameCenterCred(ENV, c, fetcher, TS);
  deep("SHA-1 signature accepted + reported as SHA-1", v,
       { identifier: GPID, idKind: "gamePlayerID", hash: "SHA-1" });
}

// ---- wrong bundle id in cred: rejected before any fetch ----
{
  const { fetcher, urls } = apple();
  // Sign for a DIFFERENT bundle id so even the crypto path couldn't pass.
  const c = cred({ bid: "com.evil.app", sig: sign(GPID, "com.evil.app", TS, SALT).toString("base64") });
  const v = await verifyGameCenterCred(ENV, c, fetcher, TS);
  eq("foreign bundle id -> null", v, null);
  eq("foreign bundle id never fetches", urls.length, 0);
}

// ---- env bundle-id override ----
{
  const { fetcher } = apple();
  const c = cred({ bid: "cc.gfm.NewtoniaBeta", sig: sign(GPID, "cc.gfm.NewtoniaBeta", TS, SALT).toString("base64") });
  const v = await verifyGameCenterCred({ GAME_CENTER_BUNDLE_ID: "cc.gfm.NewtoniaBeta" }, c, fetcher, TS);
  deep("env override accepts a matching bundle id", v,
       { identifier: GPID, idKind: "gamePlayerID", hash: "SHA-256" });
}

// ---- non-apple / non-https publicKeyURL: rejected before fetch ----
{
  const { fetcher, urls } = apple();
  eq("non-apple host -> null",
     await verifyGameCenterCred(ENV, cred({ pk: "https://evil.com/x.cer" }), fetcher, TS), null);
  eq("http (not https) -> null",
     await verifyGameCenterCred(ENV, cred({ pk: "http://static.gc.apple.com/x.cer" }), fetcher, TS), null);
  eq("bad url host never fetches", urls.length, 0);
}

// ---- stale / future timestamp: rejected before fetch ----
{
  const { fetcher, urls } = apple();
  const stale = TS - (MAX_TIMESTAMP_SKEW_MS + 1000);
  eq("stale timestamp -> null",
     await verifyGameCenterCred(ENV, cred(), fetcher, TS + MAX_TIMESTAMP_SKEW_MS + 1000), null);
  eq("future timestamp -> null",
     await verifyGameCenterCred(ENV, cred({ ts: TS + MAX_TIMESTAMP_SKEW_MS + 1000, sig: sign(GPID, BUNDLE_ID, TS + MAX_TIMESTAMP_SKEW_MS + 1000, SALT).toString("base64") }), fetcher, TS), null);
  eq("stale never fetches", urls.length, 0);
  void stale;
}

// ---- tampered signature -> verify fails -> null ----
{
  const { fetcher } = apple();
  const good = JSON.parse(cred());
  const sig = Buffer.from(good.sig, "base64");
  sig[10] ^= 0xff;                       // flip a byte
  good.sig = sig.toString("base64");
  const v = await verifyGameCenterCred(ENV, JSON.stringify(good), fetcher, TS);
  eq("tampered signature -> null", v, null);
}

// ---- salt mismatch (signed over a different salt) -> null ----
{
  const { fetcher } = apple();
  const c = cred({ sig: sign(GPID, BUNDLE_ID, TS, Buffer.from("different-salt")).toString("base64") });
  const v = await verifyGameCenterCred(ENV, c, fetcher, TS);
  eq("salt mismatch -> null", v, null);
}

// ---- malformed / missing fields ----
eq("garbage cred -> null", await verifyGameCenterCred(ENV, "not json{", apple().fetcher, TS), null);
eq("empty cred -> null", await verifyGameCenterCred(ENV, "", apple().fetcher, TS), null);
eq("oversize cred -> null", await verifyGameCenterCred(ENV, "x".repeat(MAX_CRED_LEN + 1), apple().fetcher, TS), null);
eq("no identifiers -> null",
   await verifyGameCenterCred(ENV, cred({ gpid: "", tpid: "" }), apple().fetcher, TS), null);
eq("missing sig -> null",
   await verifyGameCenterCred(ENV, JSON.stringify({ pk: PK_URL, salt: SALT.toString("base64"), ts: TS, gpid: GPID, bid: BUNDLE_ID }), apple().fetcher, TS), null);

// ---- cert fetch failures -> null ----
eq("cert http error -> null",
   await verifyGameCenterCred(ENV, cred(), async () => ({ ok: false }), TS), null);
eq("cert fetch throws -> null",
   await verifyGameCenterCred(ENV, cred(), async () => { throw new Error("net"); }, TS), null);

// ---- unparseable cert -> null (no SPKI) ----
eq("garbage cert -> null",
   await verifyGameCenterCred(ENV, cred(),
     async () => ({ ok: true, arrayBuffer: async () => Uint8Array.from([0x30, 0x01, 0x00]).buffer }), TS), null);

process.exit(failures ? 1 : 0);
