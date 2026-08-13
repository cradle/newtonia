#!/bin/bash
# Client-side cosmetic impact regression: connect via room code, skip to
# generation 3 (invincible/reflective asteroids present), both players
# spin and fire for 25 s, then assert the JOINER locally detected
# bullet-vs-asteroid impacts ("cosmetic impact" NET_LOG markers — the
# host sends no impact events since PROTO 10). Prints IMPACTS-E2E-OK.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

PA=$(launch host); sleep 2
PB=$(launch joiner); sleep 4
WINS=$(newtonia_windows); A=$(echo "$WINS" | head -1); B=$(echo "$WINS" | tail -1)
[ "$A" != "$B" ] || { echo "only one window"; exit 1; }

key $A Return; sleep 1; key $A s; key $A Return; sleep 1; key $A Return
CODE=$(host_room_code host)
[ -n "$CODE" ] || { echo "NO ROOM CODE"; kill $PA $PB; exit 1; }
echo "room code: $CODE"
nav_join $B "$CODE" joiner
sleep 18; alive $PA host; alive $PB joiner

for n in 1 2 3; do key $A n; sleep 4; done
echo "== both spin and fire 25s"
xdotool keydown --window $A space; xdotool keydown --window $A d
xdotool keydown --window $B space; xdotool keydown --window $B d
sleep 25
xdotool keyup --window $A space; xdotool keyup --window $A d
xdotool keyup --window $B space; xdotool keyup --window $B d
alive $PA host; alive $PB joiner
kill $PA $PB 2>/dev/null; wait $PA $PB 2>/dev/null
assert_clean "$OUT/host.log" "$OUT/joiner.log"
N=$(grep -ac "cosmetic impact" "$OUT/joiner.log")
echo "cosmetic impacts on joiner: $N"
[ "$N" -gt 0 ] && echo "IMPACTS-E2E-OK" || { echo "IMPACTS-E2E-FAIL (blind firing can miss; rerun once before blaming the code)"; exit 1; }
