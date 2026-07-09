#!/bin/bash
# Late-generation soak (needs a NETPLAY=1 build): two instances connect and
# the host skips level-by-level to generation 25 — through the black hole
# (9), the mini-station (10), the gen-20 station + enemy waves (PROTO 16
# ids) and the gen-20 world growth — with a liveness check at every step
# and a short two-sided firing burst every few generations. Asserts the
# 10 s snapshot telemetry actually reached gen >= 20, that the connection
# never dropped (no auto-rejoin at loopback), and clean logs. Prints
# GENSOAK-E2E-OK on success. See TESTING.md.
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

echo "== skipping to generation 25"
for g in $(seq 1 25); do
  key $A n
  sleep 3
  alive $PA host || exit 1
  alive $PB joiner || exit 1
  # Every 5th generation: a short two-sided burst (semi-auto: tap fire)
  # plus a screenshot pair for the record.
  if [ $((g % 5)) = 0 ]; then
    for i in 1 2 3; do
      xdotool key --window $A space; xdotool key --window $B space; sleep 0.3
    done
    shot $A "gensoak-host-g$g"; shot $B "gensoak-joiner-g$g"
    echo "== gen $g checkpoint alive"
  fi
done

# One more 10 s telemetry slot, then read the generation the snapshots saw.
sleep 12
alive $PA host; alive $PB joiner
GEN=$(grep -a "slot #" "$OUT/host.log" | tail -1 | sed 's/.*gen=\([0-9]*\).*/\1/')
echo "final telemetry generation: ${GEN:-none}"
[ -n "$GEN" ] && [ "$GEN" -ge 20 ] || {
  echo "FAIL: soak never reached gen 20 (telemetry says '${GEN:-none}')"
  kill $PA $PB; exit 1; }

# At loopback the session must never drop: an auto-rejoin (or the host
# losing the client) inside the soak is a real regression, not weather.
grep -aq "auto-rejoining room" "$OUT/joiner.log" && {
  echo "FAIL: joiner dropped and auto-rejoined mid-soak"; kill $PA $PB; exit 1; }
grep -aq "client lost" "$OUT/host.log" && {
  echo "FAIL: host lost the client mid-soak"; kill $PA $PB; exit 1; }

kill $PA $PB 2>/dev/null; wait $PA $PB 2>/dev/null
assert_clean "$OUT/host.log" "$OUT/joiner.log"
echo "GENSOAK-E2E-OK"
