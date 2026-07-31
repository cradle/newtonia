// Resolve a secret that may arrive in either shape, so the same verify code
// works whether a value is a per-worker Wrangler secret / plain var / test
// mock (a STRING) or a Cloudflare Secrets Store binding (an OBJECT whose
// value is read with an async .get()). Shared by the platform verifiers,
// which both the signal and board workers bundle.
//
// A missing/unreadable secret resolves to "" (the verifiers treat that as
// "unconfigured — attest nothing", never a throw), so a placeholder store
// binding in local dev can't crash a session that never actually verifies
// (the tests take the FAKE_VERIFY path and never read the secret).
export async function read_secret(v) {
  if (v == null) return "";
  if (typeof v === "string") return v;
  if (typeof v.get === "function") {
    try { return (await v.get()) || ""; } catch (e) { return ""; }
  }
  return "";
}
