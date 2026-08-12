#!/bin/bash
# Host kick (FOURPLAYER.md O3), mid-game half: a 3-seat room, then the host
# opens the pause roster and removes seat 3.
#
# What it proves, in the order the failures would bite:
#   1. the kicked peer is TOLD (EV_KICKED) rather than just dropped — a bare
#      disconnect would send it back through the rejoin door and undo this;
#   2. its card is the kicked one, not "the host left" or "connection lost";
#   3. it does NOT rejoin (the whole point of the event);
#   4. the room plays on for the host and the OTHER peer — a kick must not
#      pause or end anyone else's game (the play-on policy, PB-D7).
#
# SEATS defaults to 3: two peers, so there is a bystander to check.
set -u
cd "$(dirname "$0")"
. ./lib.sh

SEATS=${SEATS:-3}
[ "$SEATS" -ge 3 ] || { echo "SEATS must be >= 3 (need a bystander)"; exit 1; }
relay_check

room_setup "$SEATS" NEWTONIA_NET_TEST_SEATS="$SEATS"

VICTIM=$((SEATS - 1))          # last joiner index -> seat $SEATS
VICTIM_SEAT=$SEATS
BYSTANDER=1                    # joiner1, seat 2

echo "== host: pause -> PLAYERS -> select seat $VICTIM_SEAT -> kick"
HW=${ROOM_WINS[0]}
xdotool key --window "$HW" p; sleep 1          # pause
xdotool key --window "$HW" s; sleep 0.4        # RESUME -> PLAYERS
xdotool key --window "$HW" Return; sleep 0.8   # open the roster
# Row 0 is the host's own seat; step down to the victim's row.
for _ in $(seq 1 $((VICTIM_SEAT - 1))); do
  xdotool key --window "$HW" s; sleep 0.3
done
xdotool key --window "$HW" Return; sleep 0.5   # arm
xdotool key --window "$HW" Return; sleep 1.5   # confirm the kick

ok=
for _ in $(seq 1 20); do
  grep -aq "kicking player $VICTIM_SEAT" "$OUT/host.log" && { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "HOST NEVER KICKED SEAT $VICTIM_SEAT" host

echo "== the kicked peer must be told, not merely dropped"
ok=
for _ in $(seq 1 20); do
  grep -aq "kicked by the host" "$OUT/joiner$VICTIM.log" && { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "KICKED PEER NEVER GOT THE EVENT" "joiner$VICTIM"

echo "== and must not come back"
sleep 8
# Only what happened AFTER the kick counts. A kicked client goes quiet, so
# its ORIGINAL "bootstrap adopted" stays near the tail of the log and a
# plain tail|grep reports a rejoin that never happened (it did, first time
# this ran). Slice from the kick notice forward instead.
if sed -n '/kicked by the host/,$p' "$OUT/joiner$VICTIM.log" |
     grep -aq "bootstrap adopted"; then
  room_fail "KICKED PEER REJOINED" "joiner$VICTIM"
fi

echo "== the room plays on for everyone else"
# room_alive checks every still-tracked instance and tears the room down on
# a death, rather than leaving the survivors to die with the driver. The
# kicked instance is still tracked and still RUNNING — a kick ends its
# session, not its process (it sits on the removed-from-the-game card).
room_alive
grep -aq "kicked by the host" "$OUT/joiner$BYSTANDER.log" &&
  room_fail "BYSTANDER WAS KICKED TOO" "joiner$BYSTANDER"

room_kill_all
echo "NSEAT-KICK-OK (seats=$SEATS, kicked seat $VICTIM_SEAT)"
