#!/bin/bash
# B4b smoke: NEWTONIA_NET_TEST_SEATS=3 waiting room — host + 2 joiners
# through the local relay; the room auto-starts when seat 3 fills. Checks
# the host log for both seat fills + the 2-peer start, both joiner logs
# for bootstrap, then everyone flies for a few seconds.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

export NEWTONIA_NET_TEST_SEATS=3

PA=$(launch host)
sleep 2
WA=$(newtonia_windows | head -1)
nav_host "$WA"
CODE=$(host_room_code host)
[ -n "$CODE" ] || { echo "NO ROOM CODE"; kill $PA; exit 1; }
echo "room code: $CODE"

PB=$(launch joiner1)
sleep 4
WB=$(newtonia_windows | grep -v "^$WA$" | head -1)
nav_join "$WB" "$CODE"
echo "== waiting for seat 2"
for i in $(seq 1 30); do
  grep -aq "seat 2 filled" "$OUT/host.log" && break; sleep 1
done
grep -aq "seat 2 filled" "$OUT/host.log" || { echo "SEAT 2 NEVER FILLED"; tail -20 "$OUT/host.log"; kill $PA $PB; exit 1; }
echo "seat 2 filled"

PC=$(launch joiner2)
sleep 4
WC=$(newtonia_windows | grep -v "^$WA$" | grep -v "^$WB$" | head -1)
nav_join "$WC" "$CODE"
echo "== waiting for seat 3 + auto-start"
for i in $(seq 1 40); do
  grep -aq "starting with 2 peer" "$OUT/host.log" && break; sleep 1
done
grep -aq "seat 3 filled" "$OUT/host.log" || { echo "SEAT 3 NEVER FILLED"; tail -20 "$OUT/host.log"; kill $PA $PB $PC; exit 1; }
grep -aq "starting with 2 peer" "$OUT/host.log" || { echo "NO AUTO-START"; tail -20 "$OUT/host.log"; kill $PA $PB $PC; exit 1; }
echo "auto-started with 2 peers"

echo "== waiting for both bootstraps"
for i in $(seq 1 30); do
  grep -aq "bootstrap adopted" "$OUT/joiner1.log" &&
  grep -aq "bootstrap adopted" "$OUT/joiner2.log" && break; sleep 1
done
grep -aq "bootstrap adopted" "$OUT/joiner1.log" || { echo "JOINER1 NO BOOTSTRAP"; tail -20 "$OUT/joiner1.log"; kill $PA $PB $PC; exit 1; }
grep -aq "bootstrap adopted" "$OUT/joiner2.log" || { echo "JOINER2 NO BOOTSTRAP"; tail -20 "$OUT/joiner2.log"; kill $PA $PB $PC; exit 1; }
echo "both joiners bootstrapped"

echo "== everyone flies + fires for 8s"
xdotool keydown --window $WA space; xdotool keydown --window $WB space; xdotool keydown --window $WC space
xdotool keydown --window $WA w; xdotool keydown --window $WB w; xdotool keydown --window $WC w
sleep 8
xdotool keyup --window $WA space; xdotool keyup --window $WB space; xdotool keyup --window $WC space
xdotool keyup --window $WA w; xdotool keyup --window $WB w; xdotool keyup --window $WC w
sleep 2
alive $PA host; alive $PB joiner1; alive $PC joiner2
shot $WA threeseat-host; shot $WB threeseat-j1; shot $WC threeseat-j2

kill_pair $PA $PB $PC
assert_clean "$OUT/host.log" "$OUT/joiner1.log" "$OUT/joiner2.log"
echo "THREESEAT-SMOKE-OK"
