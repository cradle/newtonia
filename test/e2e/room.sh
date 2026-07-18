#!/bin/bash
# Netplay room-flow regression: two instances connect via a room code
# through the local relay, the host skips 3 levels (asteroid/pickup
# churn on the wire), both players fire for 8 s (exercises the weapon
# snapshot fast path), then the logs are checked for crashes and decode
# errors. Prints ROOM-E2E-OK on success. See TESTING.md.
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

# Host: attract -> menu -> ONLINE -> HOST (fresh prefs: NEW GAME, ONLINE)
key $A Return; sleep 1; key $A s; key $A Return; sleep 1; key $A Return

CODE=$(host_room_code host)
[ -n "$CODE" ] || { echo "NO ROOM CODE"; kill $PA $PB; exit 1; }
echo "room code: $CODE"

# Joiner: attract -> menu -> ONLINE -> JOIN -> type the code (auto-joins
# when the fifth character lands)
key $B Return; sleep 1; key $B s; key $B Return; sleep 1; key $B s; key $B Return; sleep 1
for c in $(echo "$CODE" | grep -o .); do key $B "$c"; done
echo "== waiting for connect"; sleep 18
alive $PA host; alive $PB joiner
grep -aq "bootstrap adopted" "$OUT/joiner.log" || { echo "NO BOOTSTRAP"; exit 1; }

echo "== host skips 3 levels"
for n in 1 2 3; do key $A n; sleep 4; alive $PA host; alive $PB joiner; done

echo "== both fire for 8s"
xdotool keydown --window $A space; xdotool keydown --window $B space
sleep 8
xdotool keyup --window $A space; xdotool keyup --window $B space
sleep 2
alive $PA host; alive $PB joiner
shot $A room-host; shot $B room-joiner

kill $PA $PB 2>/dev/null; wait $PA $PB 2>/dev/null
assert_clean "$OUT/host.log" "$OUT/joiner.log"
echo "ROOM-E2E-OK"
