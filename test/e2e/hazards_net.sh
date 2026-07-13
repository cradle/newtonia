#!/bin/bash
# Mid-game hazards online (needs a NETPLAY=1 build). Two instances connect and
# the host skips level-by-level to generation 12 — past the PULSAR (gen 9),
# COMET (gen 11) and SEEKER (gen 12) introductions. The host is authoritative
# for hazards; the JOINER must reconcile them from the snapshot, logging a
# "hazard replica spawned" for each kind (0=pulsar,1=comet,2=seeker) and at
# least one "hazard replica destroyed" (skipping past a hazard gen clears the
# host's, which the client bursts). Logs must stay clean. Prints HAZARD-E2E-OK.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

PA=$(launch host)
sleep 2
PB=$(launch joiner)
sleep 4

WINS=$(newtonia_windows)
A=$(echo "$WINS" | head -1); B=$(echo "$WINS" | tail -1)
[ "$A" != "$B" ] || { echo "only one window"; exit 1; }

key $A Return; sleep 1; key $A s; key $A Return; sleep 1; key $A Return
CODE=$(host_room_code host)
[ -n "$CODE" ] || { echo "NO ROOM CODE"; kill $PA $PB; exit 1; }
echo "room code: $CODE"

key $B Return; sleep 1; key $B s; key $B Return; sleep 1; key $B s; key $B Return; sleep 1
for c in $(echo "$CODE" | grep -o .); do key $B "$c"; done
echo "== waiting for connect"; sleep 18
alive $PA host; alive $PB joiner
grep -aq "bootstrap adopted" "$OUT/joiner.log" || { echo "NO BOOTSTRAP"; exit 1; }

echo "== host skips to generation 12 (through pulsar/comet/seeker)"
for g in $(seq 1 12); do
  key $A n
  sleep 3
  alive $PA host || exit 1
  alive $PB joiner || exit 1
done
# Hold on gen 12 so all three kinds replicate to the joiner, then screenshot.
sleep 4
alive $PA host; alive $PB joiner
shot $A hazards-host; shot $B hazards-joiner

kill $PA $PB 2>/dev/null; wait $PA $PB 2>/dev/null

for kind in 0 1 2; do
  grep -aq "hazard replica spawned (kind $kind)" "$OUT/joiner.log" || {
    echo "FAIL: joiner never replicated hazard kind $kind"
    exit 1; }
done
grep -aq "hazard replica destroyed" "$OUT/joiner.log" || {
  echo "FAIL: joiner never saw a hazard death (skip-clear should burst them)"
  exit 1; }
assert_clean "$OUT/host.log" "$OUT/joiner.log"
echo "joiner hazard spawns:   $(grep -ac 'hazard replica spawned' "$OUT/joiner.log")"
echo "joiner hazard deaths:   $(grep -ac 'hazard replica destroyed' "$OUT/joiner.log")"
echo "HAZARD-E2E-OK"
