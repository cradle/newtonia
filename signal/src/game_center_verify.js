// Game Center (iOS) identity verification for the signaling worker
// (NETPLAY.md V3). The joiner/host submits an identity-verification bundle
// minted client-side by GKLocalPlayer.fetchItemsForIdentityVerificationSignature
// (game_center_identity.mm): a publicKeyURL to Apple's cert, an RSA signature,
// a salt, a timestamp, and the local player's scoped identifiers + bundle id,
// packed as a compact JSON string in the `cred` field. The worker — never the
// peer — fetches Apple's public key and cryptographically verifies the
// signature, PROVING the account.
//
// ACCOUNT-ONLY (Glenn, 2026-07-22): unlike Steam/Play Games, Apple exposes NO
// server-side lookup from a verified Game Center id to its alias, and the
// signature covers only identifier+bundleID+timestamp+salt — NOT the alias
// (confirmed against Apple's docs 2026-07-22; a `fetchProfileAuthorizationCode`
// / "Game Center Management API returning alias" does not exist). So this
// verifier proves the ACCOUNT and the caller attests { platform: ios, name: "" }
// — the online iOS peer renders the IOS badge + role label. The claimed alias
// stays a p2p claim, rendered only on a worker-less (LAN) session.
//
// Trust root: no shared secret (Apple's cert is public — verifiable by anyone),
// unlike Steam/Play Games which need a publisher key/OAuth secret. The trust
// comes from (a) fetching the cert over TLS from an apple.com host (the
// publicKeyURL host is pinned to *.apple.com — a documented SSRF/spoof guard,
// the same one every server-side GC verifier applies) and (b) a tight timestamp
// freshness window (the signed blob carries no recipient binding or single-use
// tracking, so freshness is the only replay defence — NETPLAY.md V3).
//
// Two ambiguities, both resolved by trying every combination — safe because an
// RSA verify against Apple's genuine key is unforgeable, so a wrong combination
// simply fails and only a real Apple signature over one exact payload passes
// (no false positives from the extra attempts):
//   - identifier: Apple's own docs/ecosystem disagree over gamePlayerID vs
//     teamPlayerID for fetchItems (see the client TU). The client sends both;
//     we try each.
//   - digest: iOS verification is device-untested here (NETPLAY.md M3-4).
//     Modern Apple GC certs sign SHA-256, but we also try SHA-1 so a wrong
//     guess can't silently kill ALL iOS attestation with no remote debug path.

// Our app's bundle identifier (ios/project.yml PRODUCT_BUNDLE_IDENTIFIER). The
// signature binds to it, so a signature minted by a DIFFERENT app won't verify
// once we feed our own bundle id in — but we also reject a mismatched claimed
// `bid` up front (env override for a rename/test build).
export const BUNDLE_ID = "cc.gfm.Newtonia";

// Replay window: reject a bundle whose timestamp is more than this far from now
// (past OR future — clock skew cuts both ways). ~10 min per NETPLAY.md V3.
export const MAX_TIMESTAMP_SKEW_MS = 10 * 60 * 1000;

// Cap the accepted credential JSON so a peer can't flood the verifier; well
// under the worker's MAX_IDENTITY_CRED frame bound.
export const MAX_CRED_LEN = 8192;

// ---- minimal ASN.1 DER walk: extract the SubjectPublicKeyInfo from an X.509
// certificate so WebCrypto importKey('spki', ...) can consume it. Cloudflare
// Workers' WebCrypto imports an SPKI public key, not a raw X.509 cert, so we
// locate the SPKI (a SEQUENCE) by its structural position in the TBSCertificate.
function readTLV(buf, off) {
  if (off + 2 > buf.length) return null;
  const tag = buf[off];
  let len = buf[off + 1];
  let p = off + 2;
  if (len & 0x80) {
    const n = len & 0x7f;
    if (n === 0 || n > 4 || p + n > buf.length) return null; // indefinite/oversize
    len = 0;
    for (let i = 0; i < n; i++) len = len * 256 + buf[p++];
  }
  const end = p + len;
  if (end > buf.length) return null;
  return { tag, valueStart: p, end };
}

function derChildren(buf, valueStart, valueEnd) {
  const out = [];
  let p = valueStart;
  while (p < valueEnd) {
    const t = readTLV(buf, p);
    if (!t) return null;
    out.push({ tag: t.tag, start: p, end: t.end });
    p = t.end;
  }
  return out;
}

// Return the SubjectPublicKeyInfo DER (Uint8Array) from an X.509 cert DER, or
// null if the structure isn't as expected.
export function extractSPKI(der) {
  const cert = readTLV(der, 0); // Certificate ::= SEQUENCE
  if (!cert || cert.tag !== 0x30) return null;
  const tbs = readTLV(der, cert.valueStart); // tbsCertificate ::= SEQUENCE (1st)
  if (!tbs || tbs.tag !== 0x30) return null;
  const kids = derChildren(der, tbs.valueStart, tbs.end);
  if (!kids) return null;
  // TBSCertificate fields: [0] version (optional, context tag 0xA0),
  // serialNumber, signature, issuer, validity, subject, subjectPublicKeyInfo.
  const hasVersion = kids.length && kids[0].tag === 0xa0;
  const idx = hasVersion ? 6 : 5;
  const spki = kids[idx];
  if (!spki || spki.tag !== 0x30) return null;
  return der.slice(spki.start, spki.end);
}

function b64ToBytes(b64) {
  const bin = atob(b64);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}

function uint64BE(n) {
  const out = new Uint8Array(8);
  let v = BigInt(Math.trunc(n));
  for (let i = 7; i >= 0; i--) {
    out[i] = Number(v & 0xffn);
    v >>= 8n;
  }
  return out;
}

function concatBytes(parts) {
  let len = 0;
  for (const p of parts) len += p.length;
  const out = new Uint8Array(len);
  let off = 0;
  for (const p of parts) {
    out.set(p, off);
    off += p.length;
  }
  return out;
}

// Is `host` an Apple domain we'll fetch a cert from? (static.gc.apple.com,
// sandbox.gc.apple.com, ...). Exact-match apple.com or any *.apple.com subdomain.
export function is_apple_host(host) {
  return host === "apple.com" || host.endsWith(".apple.com");
}

// Verify a Game Center identity bundle. `cred` is the JSON string the client
// packed (game_center_identity.mm). Returns { identifier } on success (the
// proven scoped id) or null on any failure — a null result leaves the peer
// unattested (role-labelled); verification NEVER rejects the room.
//
// The caller attests { platform: ios, name: "" } — account only; the alias is
// never derived here (Apple provides no such lookup).
//
// `fetcher` / `now` are injectable for tests (default global fetch / Date.now).
export async function verifyGameCenterCred(env, cred, fetcher = fetch,
                                           now = Date.now()) {
  if (typeof cred !== "string" || cred.length === 0 ||
      cred.length > MAX_CRED_LEN)
    return null;
  let b;
  try {
    b = JSON.parse(cred);
  } catch (e) {
    return null;
  }
  if (!b || typeof b !== "object") return null;

  const pk = typeof b.pk === "string" ? b.pk : "";
  const sigB64 = typeof b.sig === "string" ? b.sig : "";
  const saltB64 = typeof b.salt === "string" ? b.salt : "";
  const ts = Number(b.ts);
  const gpid = typeof b.gpid === "string" ? b.gpid : "";
  const tpid = typeof b.tpid === "string" ? b.tpid : "";
  const bid = typeof b.bid === "string" ? b.bid : "";
  if (!pk || !sigB64 || !saltB64 || !Number.isFinite(ts) || (!gpid && !tpid))
    return null;

  // Bundle id must be ours: a signature minted by another app must not attest a
  // Newtonia peer. (The cert+signature check below also enforces this, since
  // the signed payload includes the bundle id — but reject early and cheaply.)
  const expectBid = (env && env.GAME_CENTER_BUNDLE_ID) || BUNDLE_ID;
  if (bid !== expectBid) return null;

  // Freshness: the signed blob has no single-use/recipient binding, so a stale
  // one is replayable — reject anything outside the window (either direction).
  if (Math.abs(now - ts) > MAX_TIMESTAMP_SKEW_MS) return null;

  // Pin the cert source to Apple over TLS (SSRF/spoof guard). A non-Apple or
  // non-https publicKeyURL attests nothing.
  let url;
  try {
    url = new URL(pk);
  } catch (e) {
    return null;
  }
  if (url.protocol !== "https:" || !is_apple_host(url.hostname)) return null;

  // Fetch Apple's public-key certificate (DER).
  let der;
  try {
    const resp = await fetcher(url.toString());
    if (!resp.ok) return null;
    der = new Uint8Array(await resp.arrayBuffer());
  } catch (e) {
    return null;
  }
  const spki = extractSPKI(der);
  if (!spki) return null;

  let sig, salt;
  try {
    sig = b64ToBytes(sigB64);
    salt = b64ToBytes(saltB64);
  } catch (e) {
    return null;
  }
  const enc = new TextEncoder();
  const bidBytes = enc.encode(bid);
  const tsBytes = uint64BE(ts);

  // Try every (identifier, digest) combination — see the header note on why
  // this is safe. Any single pass proves the account.
  const ids = [gpid, tpid].filter((s) => s.length > 0);
  const hashes = ["SHA-256", "SHA-1"];
  for (const hash of hashes) {
    let key;
    try {
      key = await crypto.subtle.importKey(
        "spki", spki, { name: "RSASSA-PKCS1-v1_5", hash }, false, ["verify"]);
    } catch (e) {
      continue; // key algo/hash mismatch — try the next hash
    }
    for (const id of ids) {
      const payload = concatBytes([enc.encode(id), bidBytes, tsBytes, salt]);
      let ok = false;
      try {
        ok = await crypto.subtle.verify("RSASSA-PKCS1-v1_5", key, sig, payload);
      } catch (e) {
        ok = false;
      }
      if (ok) return { identifier: id };
    }
  }
  return null;
}
