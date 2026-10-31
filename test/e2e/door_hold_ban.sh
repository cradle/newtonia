#!/bin/bash
# A banned account whose verification lands while the rejoin door is
# holding it (GLGame::net_host_door_hold, fork NETPLAY_XBOX.md 7e). The hold
# keeps an admitted newcomer out of the game until the room's platform
# checks answer, and the worker's attestation can arrive in that window:
# the door must re-check it against the ban list, or a banned account that
# shook its name would be seated once the checks answered (review of #633).
#
# Three desktop builds claim Xbox (NEWTONIA_NET_TEST_PLATFORM=6) against a
# FAKE_VERIFY relay, as in policy_members.sh, so the door holds:
#   MALLORY (account ACCT1) is seated in the waiting room and BANNED.
#   BOB and CAROL seat and the host starts. BOB drops.
#   EVE takes his seat: the same account ACCT1 under a new name, with a slow
#     verification ("SLOW:4500:ACCT1", the worker's dev hook) and a slow
#     platform check of her own (slow:5500), so she is admitted on her
#     claim and her account token lands while the door holds her. The host
#     turns her away from the door; she is never seated.
# Prints DOOR-HOLD-BAN-E2E-OK. See TESTING.md.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
cd "$(dirname "$0")"

PORT="${DOOR_BAN_RELAY_PORT:-8794}"
export NEWTONIA_SIGNAL_URL="ws://127.0.0.1:$PORT/ws"
. ./lib.sh

# A private wrangler state dir, as nseat_ban_token.sh does.
WSTATE=$(mktemp -d /tmp/door_hold_ban_wrangler.XXXXXX) || {
  echo "FATAL: could not create private relay state"; exit 1;
}
( cd ../../signal &&
  exec npx wrangler@4 dev --local --port "$PORT" --persist-to "$WSTATE" \
       --var FAKE_VERIFY:1 ) \
  > /tmp/door_hold_ban_wrangler.log 2>&1 &
WPID=$!
trap 'kill_tree "$WPID" 2>/dev/null; rm -rf "$WSTATE"' EXIT
echo "== starting FAKE_VERIFY relay on :$PORT (pid $WPID)"
for _ in $(seq 1 90); do
  curl -s -o /dev/null "http://127.0.0.1:$PORT/" && break
  kill -0 $WPID 2>/dev/null || { echo "relay died:"; cat /tmp/door_hold_ban_wrangler.log; exit 1; }
  sleep 1
done

relay_check
export NEWTONIA_NET_TEST_SEATS=4   # a room that stays open until START

ROOM_PIDS=(); ROOM_WINS=(); ROOM_CODE=
ROOM_PIDS[0]=$(launch host)
sleep 3
ROOM_WINS[0]=$(newtonia_windows | head -1)
[ -n "${ROOM_WINS[0]}" ] || room_fail "NO HOST WINDOW" host
HW=${ROOM_WINS[0]}
nav_host "$HW"
ROOM_CODE=$(host_room_code host)
[ -n "$ROOM_CODE" ] || room_fail "NO ROOM CODE" host
echo "room code: $ROOM_CODE"

XBOX="NEWTONIA_NET_TEST_PLATFORM=6"

# join_as SLOT LOG NAME CRED [VAR=VAL ...]: an "Xbox" pilot joins the room
join_as() {
  local slot=$1 log=$2 name=$3 cred=$4 before w
  shift 4
  before=$(newtonia_windows)
  ROOM_PIDS[$slot]=$(launch "$log" $XBOX NEWTONIA_NET_NAME="$name" \
                     NEWTONIA_NET_TEST_CRED="$cred" "$@")
  sleep 4
  w=$(new_window_since "$before")
  [ -n "$w" ] || room_fail "NO WINDOW FOR $log" "$log"
  ROOM_WINS[$slot]=$w
  nav_join "$w" "$ROOM_CODE" "$log"
}

# drop SLOT: kill a pilot outright, and wait for its window to go (X can
# hand the next instance the same window id, which would hide it from
# new_window_since; policy_members.sh)
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

echo "== MALLORY (account ACCT1) is seated, then banned"
join_as 1 mallory MALLORY ACCT1
wait_log host "seat 2 filled" 40 || room_fail "MALLORY WAS NOT SEATED" host
wait_log host "identity attested.*name='MALLORY'" 20 ||
  room_fail "MALLORY WAS NEVER ATTESTED" host
# The list rests on START GAME under the one peer row: UP lands on it,
# RIGHT swaps the action to BAN, Enter arms, Enter performs
# (nseat_ban_token.sh).
xdotool key --window "$HW" w; sleep 0.4
xdotool key --window "$HW" d; sleep 0.4
xdotool key --window "$HW" Return; sleep 0.5
xdotool key --window "$HW" Return; sleep 1.5
wait_log host "banning seat 2" 20 || room_fail "HOST NEVER BANNED SEAT 2" host
grep -aq "token yes" "$OUT/host.log" ||
  room_fail "BAN RECORDED NO ACCOUNT TOKEN" host
sleep 2   # the goodbye drains, then the seat is free
# The evader comes back as a fresh process; this one would otherwise sit on
# its REMOVED card with its relay socket open.
drop 1

echo "== BOB and CAROL seat; the host starts"
join_as 2 BOB BOB XUID-BOB
wait_count host "seat 2 filled" 2 40 || room_fail "BOB WAS NOT SEATED" host
join_as 3 CAROL CAROL XUID-CAROL
wait_log host "seat 3 filled" 40 || room_fail "CAROL WAS NOT SEATED" host
wait_log CAROL "room members from the host: 1 other joiner" 20 ||
  room_fail "CAROL NEVER HEARD THE ROOM MEMBERS" CAROL
# The ban put the cursor back to rest, where confirm means START.
xdotool key --window "$HW" Return; sleep 1
wait_log host "starting with 2 peer" 20 || room_fail "THE HOST DID NOT START" host
wait_log CAROL "bootstrap adopted" 30 || room_fail "CAROL NEVER ENTERED" CAROL
wait_log BOB "bootstrap adopted" 30 || room_fail "BOB NEVER ENTERED" BOB

echo "== BOB drops; the banned account comes back as EVE, verified late"
drop 2
wait_log host "player 2 lost" 45 || room_fail "BOB'S LOSS NEVER DETECTED" host
join_as 4 EVE EVE SLOW:4500:ACCT1 NEWTONIA_NET_TEST_PEER_VERDICT=slow:5500
wait_log host "holding the newcomer for seat 2 at the door" 45 ||
  room_fail "THE DOOR DID NOT HOLD EVE (admitted on her claim)" host
wait_log host "banned pilot attested at the door - turning away seat 2" 20 ||
  room_fail "THE DOOR DID NOT TURN THE BANNED ACCOUNT AWAY" host
wait_log EVE "removed from the room by the host" 20 ||
  room_fail "EVE WAS NEVER TOLD" EVE
grep -aq "player 2 rejoined" "$OUT/host.log" &&
  room_fail "THE BANNED ACCOUNT WAS SEATED" host
grep -aq "bootstrap adopted" "$OUT/EVE.log" &&
  room_fail "THE BANNED ACCOUNT ENTERED THE GAME" EVE
grep -aq "refused by platform policy" "$OUT/CAROL.log" &&
  room_fail "CAROL LEFT" CAROL

room_alive
room_kill_all
assert_clean "$OUT/host.log" "$OUT/BOB.log" "$OUT/CAROL.log" "$OUT/EVE.log"
echo "DOOR-HOLD-BAN-E2E-OK"
