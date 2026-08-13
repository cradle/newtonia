#!/bin/bash
# Time-slow pickup online (needs a NETPLAY=1 build, PROTO 24): the host's
# NEWTONIA_NET_TEST_TIME_SLOW_MS hook drops a real TimeSlowPickup on the
# living host ship, the ordinary collection path starts the effect
# ("time slow started"), the countdown rides the snapshots and the joiner
# adopts it ("net: time slow adopted") — both machines then pace their step
# scheduling by the same factor — and ~10 wall seconds later both sides log
# the window closing ("time slow ended" on the host; "adopted"->"ended" or
# the local countdown on the joiner). Both processes must survive with
# clean logs. Prints TIMESLOW-E2E-OK on success. See TESTING.md.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

# Drop the clock 25 s in (well past connect).
PA=$(NEWTONIA_NET_TEST_TIME_SLOW_MS=25000 launch host)
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
echo "== waiting for connect"; sleep 18
alive $PA host; alive $PB joiner
grep -aq "bootstrap adopted" "$OUT/joiner.log" || { echo "NO BOOTSTRAP"; exit 1; }

echo "== waiting for the time-slow drop + collection (25 s mark)"
for i in $(seq 1 30); do
  grep -aq "TEST dropping time-slow pickup" "$OUT/host.log" && break; sleep 1
done
grep -aq "TEST dropping time-slow pickup" "$OUT/host.log" || {
  echo "TIME-SLOW HOOK DID NOT FIRE"; kill $PA $PB; exit 1; }
# Dropped ON the ship = collected within a step or two.
for i in $(seq 1 10); do
  grep -aq "time slow started" "$OUT/host.log" && break; sleep 1
done
grep -aq "time slow started" "$OUT/host.log" || {
  echo "FAIL: pickup dropped but the effect never started"; kill $PA $PB; exit 1; }

# The countdown rides the next snapshot; the joiner logs the adopt edge.
echo "== joiner should adopt the effect"
for i in $(seq 1 10); do
  grep -aq "time slow adopted" "$OUT/joiner.log" && break; sleep 1
done
grep -aq "time slow adopted" "$OUT/joiner.log" || {
  echo "FAIL: joiner never adopted the time-slow state"; kill $PA $PB; exit 1; }
shot $A timeslow-host; shot $B timeslow-joiner

# 10 wall seconds later the window closes on both sides. The joiner's end
# can land as its own paced countdown ("time slow ended") or as the apply
# edge ("time slow ended (0 ms)") — both print "time slow ended".
echo "== waiting for the window to close (~10 s)"
for i in $(seq 1 25); do
  grep -aq "time slow ended" "$OUT/host.log" &&
    grep -aq "time slow ended" "$OUT/joiner.log" && break
  sleep 1
done
grep -aq "time slow ended" "$OUT/host.log" || {
  echo "FAIL: host effect never ended"; kill $PA $PB; exit 1; }
grep -aq "time slow ended" "$OUT/joiner.log" || {
  echo "FAIL: joiner effect never ended"; kill $PA $PB; exit 1; }
alive $PA host; alive $PB joiner

kill_pair $PA $PB
assert_clean "$OUT/host.log" "$OUT/joiner.log"
echo "TIMESLOW-E2E-OK"
