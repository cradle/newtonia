#!/bin/bash
# B7 shipping pair flow: at the DEFAULT seat cap (no NEWTONIA_NET_TEST_SEATS
# override — this is the one driver that exercises what a real install runs),
# hosting routes through the B4b waiting room, so a host + one joiner does
# NOT auto-start at 2/4: the joiner seats, the room waits, and the host's
# ENTER (the "ENTER - START GAME" row / touch TAP TO START band) starts the
# game with one peer. Asserts the seat fill, the deliberate absence of an
# auto-start, the manual start, and the joiner's bootstrap. Prints
# PAIRSTART-E2E-OK on success. Needs a local relay (see lib.sh). See
# TESTING.md.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
# lib.sh pins the classic drivers to the pairwise cap; this driver is about
# the un-pinned default, so drop the pin before any launch.
unset NEWTONIA_NET_TEST_SEATS
relay_check

PA=$(launch host)
sleep 2
PB=$(launch joiner)
sleep 4

WINS=$(newtonia_windows)
A=$(echo "$WINS" | head -1); B=$(echo "$WINS" | tail -1)
[ "$A" != "$B" ] || { echo "only one window"; exit 1; }

nav_host $A
CODE=$(host_room_code host)
[ -n "$CODE" ] || { echo "NO ROOM CODE"; kill $PA $PB; exit 1; }
echo "room code: $CODE"

nav_join $B "$CODE" joiner

echo "== waiting for the joiner to seat"
ok=0
for i in $(seq 1 45); do
  grep -aq "seat 2 filled" "$OUT/host.log" && { ok=1; break; }
  sleep 1; alive $PA host; alive $PB joiner
done
[ "$ok" = 1 ] || { echo "SEAT 2 NEVER FILLED"; kill $PA $PB; exit 1; }

# The room must WAIT at 2/4 — auto-start fires only on a full room, and on
# fill it is immediate (waiting_room_update seats and starts in the same
# tick), so a settled 3 s window is a genuine trip-wire, not a race.
sleep 3
if grep -aq "starting with" "$OUT/host.log"; then
  echo "ROOM AUTO-STARTED AT 2 SEATS"
  kill $PA $PB; exit 1
fi

echo "== host presses START"
key $A Return

ok=0
for i in $(seq 1 15); do
  grep -aq "starting with 1 peer" "$OUT/host.log" && { ok=1; break; }
  sleep 1; alive $PA host; alive $PB joiner
done
[ "$ok" = 1 ] || { echo "MANUAL START NEVER FIRED"; kill $PA $PB; exit 1; }

ok=0
for i in $(seq 1 30); do
  grep -aq "bootstrap adopted" "$OUT/joiner.log" && { ok=1; break; }
  sleep 1; alive $PA host; alive $PB joiner
done
[ "$ok" = 1 ] || { echo "NO BOOTSTRAP"; kill $PA $PB; exit 1; }

echo "== both fly for 6s"
xdotool keydown --window $A w; xdotool keydown --window $B w
sleep 6
xdotool keyup --window $A w; xdotool keyup --window $B w
sleep 2
alive $PA host; alive $PB joiner
shot $A pairstart-host; shot $B pairstart-joiner

kill_pair $PA $PB
assert_clean "$OUT/host.log" "$OUT/joiner.log"
echo "PAIRSTART-E2E-OK"
