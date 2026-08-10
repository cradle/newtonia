#!/bin/bash
# B6 4-seat suite: connect/play, drop/rejoin and game-over at SEATS=4,
# each driver under its own Xvfb. SOAK=1 appends the generation soak.
# Prints FOURSEAT-E2E-OK. Run locally/on-demand (CI stays 2-instance —
# runner cost); needs a local relay (see lib.sh). See TESTING.md.
set -u
cd "$(dirname "$0")"
for d in nseat.sh nseat_rejoin.sh nseat_gameover.sh; do
  echo "== $d (SEATS=4)"
  SEATS=4 "./$d" || { echo "FOURSEAT-E2E-FAIL: $d"; exit 1; }
done
if [ "${SOAK:-0}" = 1 ]; then
  echo "== nseat_soak.sh (SEATS=4)"
  SEATS=4 ./nseat_soak.sh || { echo "FOURSEAT-E2E-FAIL: nseat_soak.sh"; exit 1; }
fi
echo "FOURSEAT-E2E-OK"
