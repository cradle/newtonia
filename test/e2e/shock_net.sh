#!/bin/bash
# PROTO 22 regression: the Shock chain-lightning primary online. Both
# instances launch with NEWTONIA_ALL_WEAPONS=1 (grants the full arsenal each
# life and auto-selects Shock, the last primary added). Two players connect,
# each fires shock repeatedly; the PEER must log "shock bolt received"
# (MSG_SHOCK) both directions — the receiver shows the firer's EXACT segments,
# never a re-seek. Logs must stay clean (no decode/UAF). Prints SHOCK-E2E-OK.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

export NEWTONIA_ALL_WEAPONS=1
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

# Shock is SEMI-AUTOMATIC: one bolt per trigger PULL, no hold-to-repeat. So
# each round is a discrete press (keydown/hold/keyup = a single pull = a single
# bolt) — six rounds ⇒ ~6 bolts per side, NOT the ~40 an automatic weapon would
# emit while held. That drop is itself the semi-auto proof. Thrust a little
# between pulls so the bolts seek asteroids.
echo "== both fire shock (one pull per round) while drifting"
for round in 1 2 3 4 5 6; do
  xdotool keydown --window $A space; xdotool keydown --window $B space
  sleep 1.2
  xdotool keyup --window $A space; xdotool keyup --window $B space
  key $A w; key $B w
  sleep 0.6
done
sleep 2

alive $PA host; alive $PB joiner
grep -aq "shock bolt received" "$OUT/host.log" || {
  echo "FAIL: client shock bolt never reached the host (MSG_SHOCK C->H)"
  kill $PA $PB; exit 1; }
grep -aq "shock bolt received" "$OUT/joiner.log" || {
  echo "FAIL: host shock bolt never reached the client (MSG_SHOCK H->C)"
  kill $PA $PB; exit 1; }
shot $A shock-host; shot $B shock-joiner

kill $PA $PB 2>/dev/null; wait $PA $PB 2>/dev/null
assert_clean "$OUT/host.log" "$OUT/joiner.log"
echo "host  shock bolts received: $(grep -ac 'shock bolt received' "$OUT/host.log")"
echo "joiner shock bolts received: $(grep -ac 'shock bolt received' "$OUT/joiner.log")"
echo "SHOCK-E2E-OK"
