// Unit test for read_secret (src/secret.js): the one place that absorbs
// the two shapes a worker secret arrives in — a per-worker Wrangler
// secret / var / test mock (a STRING) or a Secrets Store binding (an
// OBJECT with an async .get()). The verifiers rely on every failure mode
// resolving to "" ("unconfigured — attest nothing"), NEVER throwing: a
// store outage or a placeholder binding must degrade a verification to
// unattested, not crash the session mid-handshake.
// Run: node test/secret_test.mjs
import { read_secret } from "../src/secret.js";

let failures = 0;
function eq(name, a, b) {
  const ok = a === b;
  console.log((ok ? "PASS " : "FAIL ") + name + (ok ? "" : `  (${a} !== ${b})`));
  if (!ok) failures++;
}

// Plain strings pass straight through (per-worker secrets, vars, mocks).
eq("string passes through", await read_secret("hunter2"), "hunter2");
eq("empty string stays empty", await read_secret(""), "");

// Absent binding: unconfigured, not an error.
eq("undefined -> empty", await read_secret(undefined), "");
eq("null -> empty", await read_secret(null), "");

// Secrets Store binding: the async .get() value.
eq("store get resolves", await read_secret({ get: async () => "s3cret" }),
   "s3cret");

// Store failure modes all degrade to "" — never a throw.
eq("store get throws -> empty",
   await read_secret({ get: async () => { throw new Error("store down"); } }),
   "");
eq("store get rejects -> empty",
   await read_secret({ get: () => Promise.reject(new Error("no perms")) }),
   "");
eq("store get null -> empty", await read_secret({ get: async () => null }), "");
eq("store get undefined -> empty",
   await read_secret({ get: async () => undefined }), "");
// A SYNCHRONOUS throw from .get() itself (not a rejected promise) is the
// sneaky one — the try must wrap the call, not just the await.
eq("store get sync-throws -> empty",
   await read_secret({ get: () => { throw new Error("sync"); } }), "");

// Shapes that are neither: unreadable, so unconfigured.
eq("object without get -> empty", await read_secret({ value: "x" }), "");
eq("number -> empty", await read_secret(42), "");

process.exit(failures ? 1 : 0);
