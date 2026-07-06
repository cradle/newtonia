#!/bin/bash
# M2-4/M3-1 rejoin regression: connect via room code, SIGKILL the joiner
# mid-game, relaunch it and rejoin with the SAME code, verify the host
# parked the remote ship ("player 2 lost - room ... reopened") and
# resumed the session ("player 2 rejoined"). Prints REJOIN-E2E-OK on
# success. See TESTING.md.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

join_with_code() {  # WINDOW CODE: menu -> ONLINE -> JOIN -> type code
  local W=$1 CODE=$2 c
  key $W Return; sleep 1; key $W s; key $W Return; sleep 1
  key $W s; key $W Return; sleep 1
  for c in $(echo "$CODE" | grep -o .); do key $W "$c"; done
}

PA=$(launch host)
sleep 2
PB=$(launch joiner1)
sleep 4

WINS=$(newtonia_windows)
A=$(echo "$WINS" | head -1); B=$(echo "$WINS" | tail -1)
[ "$A" != "$B" ] || { echo "only one window"; exit 1; }

key $A Return; sleep 1; key $A s; key $A Return; sleep 1; key $A Return
CODE=$(host_room_code host)
[ -n "$CODE" ] || { echo "NO ROOM CODE"; kill $PA $PB; exit 1; }
echo "room code: $CODE"

join_with_code $B "$CODE"
echo "== waiting for connect + play"; sleep 18
alive $PA host; alive $PB joiner1

echo "== SIGKILL the joiner mid-game"
kill -9 $PB; sleep 4
alive $PA host
shot $A rejoin-host-waiting

echo "== relaunch joiner, rejoin with $CODE"
PB=$(launch joiner2)
sleep 4
for w in $(newtonia_windows); do [ "$w" != "$A" ] && B=$w; done
join_with_code $B "$CODE"
echo "== waiting for rejoin"; sleep 18
alive $PA host; alive $PB joiner2
shot $A rejoin-host-resumed; shot $B rejoin-joiner-resumed

kill $PA $PB 2>/dev/null; wait $PA $PB 2>/dev/null
assert_clean "$OUT/host.log" "$OUT/joiner1.log" "$OUT/joiner2.log"
grep -aq "player 2 lost" "$OUT/host.log" || { echo "NO PARK MARKER"; exit 1; }
grep -aq "player 2 rejoined" "$OUT/host.log" || { echo "REJOIN-E2E-FAIL"; exit 1; }
echo "REJOIN-E2E-OK"
