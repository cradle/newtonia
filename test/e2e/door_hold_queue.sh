#!/bin/bash
# Platform checks that queue up (GLGame::net_host_door_hold, fork
# NETPLAY_XBOX.md 7e; review of #633):
#   - in the waiting room, one player's checks of two later joiners each get
#     the full verdict wait (NetLobby PEER_VERDICT_WAIT_MS), not what the
#     earlier check left over;
#   - mid-game, two newcomers arriving together: the rejoin door takes one
#     at a time, and a held newcomer keeps it busy, so the second waits its
#     turn and is then checked against the first like anyone seated.
#
# Desktop builds claim Xbox (NEWTONIA_NET_TEST_PLATFORM=6) against a
# FAKE_VERIFY relay, as in policy_members.sh. "slow-each:N" makes a pilot's
# check of each other player take N ms, from the first time it asks.
#   BOB (slow-each:4000) seats: 4 s on the host. CAROL seats: 4 s more on
#     her, which used to find the 6 s budget 4 s spent and make BOB leave.
#     DAVE seats and the full room starts.
#   CAROL and DAVE drop. DAN (slow-each:3000) takes seat 3 and is held
#     while the checks run; FRAN (blocks DAN) arrives during that hold and
#     waits. DAN is seated, then FRAN is held for seat 4 and leaves from the
#     door, because DAN was here first.
# Prints DOOR-HOLD-QUEUE-E2E-OK. See TESTING.md.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
cd "$(dirname "$0")"

PORT="${DOOR_QUEUE_RELAY_PORT:-8795}"
export NEWTONIA_SIGNAL_URL="ws://127.0.0.1:$PORT/ws"
. ./lib.sh

# A private wrangler state dir, as nseat_anon.sh does.
WSTATE=$(mktemp -d /tmp/door_hold_queue_wrangler.XXXXXX) || {
  echo "FATAL: could not create private relay state"; exit 1;
}
( cd ../../signal &&
  exec npx wrangler@4 dev --local --port "$PORT" --persist-to "$WSTATE" \
       --var FAKE_VERIFY:1 ) \
  > /tmp/door_hold_queue_wrangler.log 2>&1 &
WPID=$!
trap 'kill_tree "$WPID" 2>/dev/null; rm -rf "$WSTATE"' EXIT
echo "== starting FAKE_VERIFY relay on :$PORT (pid $WPID)"
for _ in $(seq 1 90); do
  curl -s -o /dev/null "http://127.0.0.1:$PORT/" && break
  kill -0 $WPID 2>/dev/null || { echo "relay died:"; cat /tmp/door_hold_queue_wrangler.log; exit 1; }
  sleep 1
done

relay_check
export NEWTONIA_NET_TEST_SEATS=4   # a room that stays open until START

ROOM_PIDS=(); ROOM_WINS=(); ROOM_CODE=
ROOM_PIDS[0]=$(launch host NEWTONIA_NET_NAME=HOST)
sleep 3
ROOM_WINS[0]=$(newtonia_windows | head -1)
[ -n "${ROOM_WINS[0]}" ] || room_fail "NO HOST WINDOW" host
HW=${ROOM_WINS[0]}
nav_host "$HW"
ROOM_CODE=$(host_room_code host)
[ -n "$ROOM_CODE" ] || room_fail "NO ROOM CODE" host
echo "room code: $ROOM_CODE"

XBOX="NEWTONIA_NET_TEST_PLATFORM=6"

# join_as SLOT NAME [VAR=VAL ...]: an "Xbox" pilot joins the room
join_as() {
  local slot=$1 name=$2 before w
  shift 2
  before=$(newtonia_windows)
  ROOM_PIDS[$slot]=$(launch "$name" $XBOX NEWTONIA_NET_NAME="$name" \
                     NEWTONIA_NET_TEST_CRED="XUID-$name" "$@")
  sleep 4
  w=$(new_window_since "$before")
  [ -n "$w" ] || room_fail "NO WINDOW FOR $name" "$name"
  ROOM_WINS[$slot]=$w
  nav_join "$w" "$ROOM_CODE" "$name"
}

# drop SLOT: kill a pilot outright, and wait for its window to go (X can
# hand the next instance the same window id; policy_members.sh)
drop() {
  local w=${ROOM_WINS[$1]} i
  kill -9 "${ROOM_PIDS[$1]}"; wait "${ROOM_PIDS[$1]}" 2>/dev/null
  ROOM_PIDS[$1]=; ROOM_WINS[$1]=
  for i in $(seq 1 20); do
    newtonia_windows | grep -q "^$w$" || return 0
    sleep 0.5
  done
}

wait_log() {
  local i
  for i in $(seq 1 "$3"); do
    grep -aq "$2" "$OUT/$1.log" && return 0
    sleep 1
  done
  return 1
}
count_log() { grep -ac "$2" "$OUT/$1.log"; }
wait_count() {
  local i
  for i in $(seq 1 "$4"); do
    [ "$(count_log "$1" "$2")" -ge "$3" ] && return 0
    sleep 1
  done
  return 1
}
line_of() { grep -an "$2" "$OUT/$1.log" | head -1 | cut -d: -f1; }

ALLOWED="platform allowed everyone here after"

echo "== BOB seats; his check of the host takes 4 s"
join_as 1 BOB NEWTONIA_NET_TEST_PEER_VERDICT=slow-each:4000
wait_log host "seat 2 filled" 40 || room_fail "BOB WAS NOT SEATED" host
wait_log BOB "telling the host we allow everyone in the room" 30 ||
  room_fail "BOB NEVER ALLOWED THE HOST" BOB

echo "== CAROL seats; BOB's check of her takes another 4 s"
join_as 2 CAROL
wait_log host "seat 3 filled" 40 || room_fail "CAROL WAS NOT SEATED" host
# Past the 4 s check and the 6 s budget: BOB is still here.
sleep 7
grep -aq "refused by platform policy" "$OUT/BOB.log" &&
  room_fail "BOB LEFT: HIS SECOND CHECK GOT THE FIRST ONE'S LEFTOVER TIME" BOB
wait_count BOB "$ALLOWED" 2 10 || room_fail "BOB NEVER ALLOWED CAROL" BOB
ms=$(grep -a "$ALLOWED" "$OUT/BOB.log" | sed -n 2p |
     sed 's/.*after \([0-9]*\) ms.*/\1/')
[ -n "$ms" ] && [ "$ms" -ge 3000 ] ||
  room_fail "BOB'S CHECK OF CAROL WAS TIMED FROM THE FIRST (${ms:-no line})" BOB

echo "== DAVE seats; a full room starts by itself"
join_as 3 DAVE
wait_log host "seat 4 filled" 40 || room_fail "DAVE WAS NOT SEATED" host
wait_log host "starting with 3 peer" 20 || room_fail "THE HOST DID NOT START" host
# The snapshot can beat BOB's check of DAVE, so its answer may read either way.
wait_count BOB "platform allowed" 3 30 || room_fail "BOB NEVER ALLOWED DAVE" BOB
for n in BOB CAROL DAVE; do
  wait_log $n "bootstrap adopted" 30 || room_fail "$n NEVER ENTERED" $n
done

echo "== CAROL and DAVE drop"
drop 2
drop 3
wait_log host "seat 3 lost" 45 || room_fail "CAROL'S LOSS NEVER DETECTED" host
wait_log host "seat 4 lost" 45 || room_fail "DAVE'S LOSS NEVER DETECTED" host

echo "== DAN and FRAN arrive together; FRAN blocks DAN"
# Both launched first, then the two codes typed back to back.
before=$(newtonia_windows)
ROOM_PIDS[4]=$(launch DAN $XBOX NEWTONIA_NET_NAME=DAN NEWTONIA_NET_TEST_CRED=XUID-DAN \
               NEWTONIA_NET_TEST_PEER_VERDICT=slow-each:3000)
sleep 4
ROOM_WINS[4]=$(new_window_since "$before")
[ -n "${ROOM_WINS[4]}" ] || room_fail "NO WINDOW FOR DAN" DAN
before=$(newtonia_windows)
ROOM_PIDS[5]=$(launch FRAN $XBOX NEWTONIA_NET_NAME=FRAN NEWTONIA_NET_TEST_CRED=XUID-FRAN \
               NEWTONIA_NET_TEST_PEER_VERDICT=refuse-name:DAN)
sleep 4
ROOM_WINS[5]=$(new_window_since "$before")
[ -n "${ROOM_WINS[5]}" ] || room_fail "NO WINDOW FOR FRAN" FRAN
# nav_join's menu steps, for both windows at once, so both reach the room
# within a second of each other (each nav_join alone takes several).
D=${ROOM_WINS[4]}; F=${ROOM_WINS[5]}
dj=$(join_count DAN); fj=$(join_count FRAN)
key "$D" Return; key "$F" Return; sleep 1
key "$D" s; key "$F" s; key "$D" Return; key "$F" Return; sleep 1
key "$D" s; key "$F" s; key "$D" Return; key "$F" Return; sleep 1
for _ in $(seq 1 12); do
  [ "$(join_count DAN)" -gt "$dj" ] && [ "$(join_count FRAN)" -gt "$fj" ] && break
  sleep 0.5
done
for who in DAN FRAN; do
  [ "$who" = DAN ] && w=$D n=$dj || w=$F n=$fj
  [ "$(join_count $who)" -gt "$n" ] && continue
  for c in $(printf '%s' "$ROOM_CODE" | tr '[:upper:]' '[:lower:]' | grep -o .); do
    key "$w" "$c"
  done
done
# FRAN is in the room before DAN is seated: the two are at the door together.
grep -aq "player 3 rejoined" "$OUT/host.log" &&
  room_fail "DAN WAS SEATED BEFORE FRAN ARRIVED (no queue to test)" host
wait_log host "holding the newcomer for seat 3 at the door" 45 ||
  room_fail "THE DOOR DID NOT HOLD DAN" host
wait_log host "player 3 rejoined" 30 || room_fail "DAN WAS NOT SEATED" host
wait_log host "door checks for seat 3 done after" 1 ||
  room_fail "DAN WAS SEATED WITHOUT HIS CHECKS ANSWERING" host
wait_log DAN "bootstrap adopted" 30 || room_fail "DAN NEVER ENTERED" DAN
wait_log host "holding the newcomer for seat 4 at the door" 45 ||
  room_fail "THE DOOR NEVER TOOK FRAN" host
held4=$(line_of host "holding the newcomer for seat 4 at the door")
seated3=$(line_of host "player 3 rejoined")
[ "$seated3" -lt "$held4" ] ||
  room_fail "FRAN WAS HELD BEFORE DAN WAS SEATED" host
wait_log FRAN "another player in the room refused by platform policy" 20 ||
  room_fail "FRAN DID NOT LEAVE A ROOM WITH DAN IN IT" FRAN
wait_log host "the newcomer for seat 4 left the door" 30 ||
  room_fail "THE HOST NEVER SAW FRAN LEAVE THE DOOR" host
grep -aq "player 4 rejoined" "$OUT/host.log" &&
  room_fail "FRAN WAS SEATED" host
grep -aq "bootstrap adopted" "$OUT/FRAN.log" &&
  room_fail "FRAN ENTERED THE GAME" FRAN
grep -aq "unanswered after" "$OUT/host.log" &&
  room_fail "THE DOOR TIMED OUT INSTEAD OF HEARING THE ANSWERS" host
for n in BOB DAN; do
  grep -aq "refused by platform policy" "$OUT/$n.log" &&
    room_fail "$n LEFT" $n
done
drop 5

room_alive
room_kill_all
assert_clean "$OUT/host.log" "$OUT/BOB.log" "$OUT/CAROL.log" "$OUT/DAVE.log" \
             "$OUT/DAN.log" "$OUT/FRAN.log"
echo "DOOR-HOLD-QUEUE-E2E-OK"
