#!/bin/bash
# B5 smoke: per-seat rejoin with play-on. A 3-seat room auto-starts, seat
# 3's client is SIGKILLed mid-game — the host must mark PLAYER 3 lost and
# keep playing UNPAUSED (PB-D7: another remote peer is still in it), park
# only that hull, and rejoin a relaunched client onto seat 3. Prints
# THREESEAT-REJOIN-OK on success. Needs a local relay (see lib.sh).
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
for i in $(seq 1 30); do grep -aq "seat 2 filled" "$OUT/host.log" && break; sleep 1; done
grep -aq "seat 2 filled" "$OUT/host.log" || { echo "SEAT 2 NEVER FILLED"; kill $PA $PB; exit 1; }

PC=$(launch joiner2)
sleep 4
WC=$(newtonia_windows | grep -v "^$WA$" | grep -v "^$WB$" | head -1)
nav_join "$WC" "$CODE"
for i in $(seq 1 40); do grep -aq "starting with 2 peer" "$OUT/host.log" && break; sleep 1; done
grep -aq "starting with 2 peer" "$OUT/host.log" || { echo "NO AUTO-START"; kill $PA $PB $PC; exit 1; }
for i in $(seq 1 30); do
  grep -aq "bootstrap adopted" "$OUT/joiner1.log" &&
  grep -aq "bootstrap adopted" "$OUT/joiner2.log" && break; sleep 1
done
grep -aq "bootstrap adopted" "$OUT/joiner2.log" || { echo "JOINER2 NO BOOTSTRAP"; kill $PA $PB $PC; exit 1; }
echo "3-seat game up"

# Everyone plays a few seconds so seat 3 has INPUT flowing (the RX
# watchdog needs have_input), then kill seat 3's client outright.
xdotool keydown --window $WA space; xdotool keydown --window $WB space; xdotool keydown --window $WC space
sleep 4
xdotool keyup --window $WA space; xdotool keyup --window $WB space; xdotool keyup --window $WC space
echo "== SIGKILL seat 3's client"
kill -9 $PC; wait $PC 2>/dev/null

# Loss detect (ICE failure or the 10 s RX watchdog) -> park seat 3, doors
# reopen. PB-D7: play CONTINUES - the host must NOT auto-pause.
LOST=
for i in $(seq 1 45); do
  grep -aq "player 3 lost" "$OUT/host.log" && { LOST=1; break; }
  sleep 1
done
[ -n "$LOST" ] || { echo "SEAT 3 LOSS NEVER DETECTED"; tail -12 "$OUT/host.log"; kill $PA $PB; exit 1; }
grep -aq "paused awaiting rejoin" "$OUT/host.log" && { echo "PAUSED WITH A LIVE PEER (play-on violated)"; kill $PA $PB; exit 1; }
alive $PA host; alive $PB joiner1
echo "seat 3 lost, play continued"

# Relaunch seat 3's player: a rejoin is a plain JOIN with the same code.
PD=$(launch joiner3)
sleep 4
WD=$(newtonia_windows | grep -v "^$WA$" | grep -v "^$WB$" | head -1)
nav_join "$WD" "$CODE"
REJOINED=
for i in $(seq 1 45); do
  grep -aq "player 3 rejoined" "$OUT/host.log" && { REJOINED=1; break; }
  sleep 1
done
[ -n "$REJOINED" ] || { echo "SEAT 3 NEVER REJOINED"; tail -12 "$OUT/host.log"; kill $PA $PB $PD; exit 1; }
grep -aq "bootstrap adopted" "$OUT/joiner3.log" || { echo "REJOINER NO BOOTSTRAP"; kill $PA $PB $PD; exit 1; }
echo "seat 3 rejoined"

sleep 3
alive $PA host; alive $PB joiner1; alive $PD joiner3
shot $WA tsr-host; shot $WB tsr-j1; shot $WD tsr-j3

kill_pair $PA $PB $PD
assert_clean "$OUT/host.log" "$OUT/joiner1.log" "$OUT/joiner2.log" "$OUT/joiner3.log"
echo "THREESEAT-REJOIN-OK"
