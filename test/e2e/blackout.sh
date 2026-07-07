#!/bin/bash
# Real 1-second backlog test: connect at gen 8, fly, then black-hole ALL
# loopback UDP for 1.2 s three times (real SCTP retransmit backlogs, the
# exact condition behind "asteroids move forward, then back, then jump
# forward again"). Verdict: stale-drop lines must fire, and no asteroid
# render jump may follow a blackout (teleports excepted).
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
key $B Return; sleep 1; key $B s; key $B Return; sleep 1; key $B s; key $B Return; sleep 1
for c in $(echo "$CODE" | grep -o .); do key $B "$c"; done
sleep 18
alive $PA host; alive $PB joiner
grep -aq "bootstrap adopted" "$OUT/joiner.log" || { echo "NO BOOTSTRAP"; exit 1; }

for n in 1 2 3 4 5 6 7; do
  key $A n; sleep 3; key $A space; key $B space; sleep 2
done
alive $PA host; alive $PB joiner

xdotool keydown --window $A w; xdotool keydown --window $B w
echo "== 3 x 1.2s TOTAL UDP blackouts, 8s apart"
for i in 1 2 3; do
  sleep 8
  /usr/sbin/iptables -A INPUT -i lo -p udp -j DROP
  sleep 1.2
  /usr/sbin/iptables -D INPUT -i lo -p udp -j DROP
  echo "blackout $i done"
done
sleep 8
xdotool keyup --window $A w; xdotool keyup --window $B w
alive $PA host; alive $PB joiner

kill $PA $PB 2>/dev/null; wait $PA $PB 2>/dev/null
echo "== stale drops: $(grep -ac 'dropped stale' "$OUT/joiner.log")"
grep -a 'dropped stale' "$OUT/joiner.log" | head -6
echo "== asteroid render jumps after blackouts:"
grep -a 'render jump' "$OUT/joiner.log" | grep -av SHIP | head -10
echo "== reconcile:"
grep -a 'reconcile' "$OUT/joiner.log" | tail -8
echo "== input gaps: $(grep -ac 'input gap' "$OUT/host.log")"
echo "BLACKOUT-DONE out=$OUT"
