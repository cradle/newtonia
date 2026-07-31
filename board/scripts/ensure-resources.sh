#!/usr/bin/env bash
# Idempotent Cloudflare resource bootstrap for the leaderboard worker
# (LEADERBOARD.md L4 / deployment automation). No Terraform: four resources
# (two D1 databases, two R2 buckets) that never change shape are fully
# managed by the wrangler CLI, so the deploy just ensures they exist and
# resolves the real database_id into the config at deploy time — the
# wrangler.toml keeps an obvious placeholder id in git (local `wrangler dev`
# ignores it).
#
# Usage:  ensure-resources.sh <production|beta>
# Prints: DATABASE_ID=<uuid>   (for the caller to inject into wrangler.toml)
# Requires: CLOUDFLARE_API_TOKEN + CLOUDFLARE_ACCOUNT_ID in the environment,
#           and the token must additionally allow D1:Edit and
#           "Workers R2 Storage:Edit" (the deploy token only needs
#           Workers Scripts:Edit, so grant these to the SAME token or run
#           this step with a broader one — see board/README.md).
set -euo pipefail

TARGET="${1:?usage: ensure-resources.sh <production|beta>}"
case "$TARGET" in
  production) DB="newtonia-board";      BUCKET="newtonia-replays" ;;
  beta)       DB="newtonia-board-beta"; BUCKET="newtonia-replays-beta" ;;
  *) echo "unknown target '$TARGET' (want production or beta)" >&2; exit 1 ;;
esac

WR="npx --yes wrangler@4"

# --- D1: create if missing, then resolve the id by name -------------------
# `d1 list --json` is the source of truth; `d1 create` is only attempted
# when the name is absent (create on an existing name errors).
db_id() {
  $WR d1 list --json 2>/dev/null \
    | node -e 'const n=process.argv[1];let s="";process.stdin.on("data",d=>s+=d).on("end",()=>{try{const a=JSON.parse(s);const r=(Array.isArray(a)?a:[]).find(x=>x.name===n);process.stdout.write(r&&(r.uuid||r.database_id)||"")}catch(e){}})' "$DB"
}

ID="$(db_id || true)"
if [ -z "$ID" ]; then
  echo "d1: creating $DB" >&2
  # Create is idempotent-enough for our use: if a race created it, the
  # follow-up list still resolves the id (we ignore create's own error).
  $WR d1 create "$DB" >&2 || true
  ID="$(db_id || true)"
fi
[ -n "$ID" ] || { echo "d1: could not create or find $DB" >&2; exit 1; }
echo "d1: $DB -> $ID" >&2

# --- R2: create the bucket if missing -------------------------------------
# Attempt the create and treat an "already exists" failure as success —
# more robust than parsing `r2 bucket list` output across wrangler versions.
if ! CREATE_OUT="$($WR r2 bucket create "$BUCKET" 2>&1)"; then
  if echo "$CREATE_OUT" | grep -qiE "already|exists|10004"; then
    echo "r2: $BUCKET exists" >&2
  else
    echo "r2: failed to create $BUCKET:" >&2
    echo "$CREATE_OUT" >&2
    exit 1
  fi
else
  echo "r2: created $BUCKET" >&2
fi

echo "DATABASE_ID=$ID"
