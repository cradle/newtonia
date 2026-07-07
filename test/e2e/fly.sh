#!/bin/bash
# Heavy jitter probe: gen 8 (~100 rocks), BOTH ships flying for 45 s,
# under 2% real UDP loss (iptables, applied outside) + 30 ms RX delay
# (NEWTONIA_NET_TEST_RX_DELAY_MS) — the closest loopback approximation
# of Glenn's relay session. Verdict comes from the joiner's render-jump
# and reconcile lines.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
export NEWTONIA_NET_TEST_RX_DELAY_MS=30
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

echo "== skip to gen 8"
for n in 1 2 3 4 5 6 7; do
  key $A n; sleep 3; key $A space; key $B space; sleep 2
  alive $PA host; alive $PB joiner
done

echo "== both ships FLY for 45s (thrust + rotate held)"
xdotool keydown --window $A w; xdotool keydown --window $A a
xdotool keydown --window $B w; xdotool keydown --window $B d
sleep 45
xdotool keyup --window $A w; xdotool keyup --window $A a
xdotool keyup --window $B w; xdotool keyup --window $B d
alive $PA host; alive $PB joiner

kill $PA $PB 2>/dev/null; wait $PA $PB 2>/dev/null
echo "== joiner verdict:"
grep -a 'render jump' "$OUT/joiner.log" | grep -av tele | head -20
echo "render jumps total: $(grep -ac 'render jump' "$OUT/joiner.log")"
grep -a 'reconcile' "$OUT/joiner.log" | tail -10
grep -ac 'input gap' "$OUT/host.log" || true
echo "GEN8FLY-DONE out=$OUT"
