#!/usr/bin/env bash
# Deploy-prerequisite check for the signal/room worker — the parity of the
# board worker's board/scripts/ensure-resources.sh.
#
# Unlike the board (D1 database + R2 bucket that must be CREATED and whose
# id must be injected), the signal worker's ONLY infra is its two Durable
# Objects (Room, Limiter). Durable Object namespaces are provisioned
# AUTOMATICALLY by `wrangler deploy` from the [[migrations]] in
# wrangler.toml — there is nothing to pre-create and no id to paste. So
# this script does NOT create anything; it fail-fasts on the deploy
# prerequisites (clear message instead of a cryptic wrangler error) and
# guards against a future resource type being added to the config without
# the matching bootstrap.
#
# Usage:  ensure-resources.sh <production|beta>
# Requires: CLOUDFLARE_API_TOKEN + CLOUDFLARE_ACCOUNT_ID in the environment.
set -euo pipefail

TARGET="${1:?usage: ensure-resources.sh <production|beta>}"
case "$TARGET" in
  production|beta) ;;
  *) echo "unknown target '$TARGET' (want production or beta)" >&2; exit 1 ;;
esac

TOML="$(dirname "$0")/../wrangler.toml"

# Prerequisites: fail fast with a readable message.
: "${CLOUDFLARE_API_TOKEN:?set CLOUDFLARE_API_TOKEN (Workers Scripts:Edit)}"
: "${CLOUDFLARE_ACCOUNT_ID:?set CLOUDFLARE_ACCOUNT_ID}"

# Guard: the signal worker is Durable-Objects-only. If a resource type that
# DOES need bootstrapping (D1 / R2 / KV) is ever added to wrangler.toml, this
# script must grow to create it — fail loudly rather than deploy a worker
# whose binding points at a resource that was never created.
if grep -qE '^\s*\[\[(d1_databases|r2_buckets|kv_namespaces)\]\]' "$TOML"; then
  echo "signal ensure: wrangler.toml now declares a D1/R2/KV resource, but" >&2
  echo "  this script only knows how to provision Durable Objects. Add the" >&2
  echo "  create/resolve step (see board/scripts/ensure-resources.sh) before" >&2
  echo "  deploying, or the binding will point at a resource that does not" >&2
  echo "  exist." >&2
  exit 1
fi

# Secrets Store: inject the shared store id over the placeholder — the SAME
# account-level store the board worker binds, so one copy of the verify
# secrets serves both. The store + secrets are created once by hand (its
# VALUES can't be automated); the id rides the CF_SECRETS_STORE_ID repo
# variable. Skip cleanly when unset (the worker then falls back to any
# per-worker secrets still set on it).
STORE_PLACEHOLDER="00000000000000000000000000000000"
if [ -n "${CF_SECRETS_STORE_ID:-}" ]; then
  sed -i "s/$STORE_PLACEHOLDER/$CF_SECRETS_STORE_ID/g" "$TOML"
  echo "secrets-store: bound $CF_SECRETS_STORE_ID" >&2
else
  echo "secrets-store: CF_SECRETS_STORE_ID unset — leaving placeholder" >&2
  echo "  (see README.md; a build with per-worker secrets still verifies)." >&2
fi

# Report what will be provisioned. Durable Object classes are created (and
# their SQLite storage migrated) by the deploy itself from [[migrations]];
# nothing to do here.
DO_COUNT="$(grep -cE '^\s*\[\[(env\.beta\.)?durable_objects\.bindings\]\]' "$TOML" || true)"
echo "signal ensure ($TARGET): Durable-Objects-only infra — nothing to pre-create." >&2
echo "  $DO_COUNT DO binding declaration(s); namespaces auto-provision via [[migrations]] on deploy." >&2
