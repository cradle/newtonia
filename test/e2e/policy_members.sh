#!/bin/bash
# Per-peer platform policy across a 3-4 player room (net_policy.h
# net_peer_verdict; the Xbox fork's block-list check, fork NETPLAY_XBOX.md
# 7c). A joiner must be checked against every player it will play with, not
# only the host: the worker tells Xbox-verified joiners who the other
# joiners are, and the host tells each joiner which of them are seated with
# it (MSG_ROOM_MEMBERS).
#
# Three desktop builds stand in for Xbox consoles (NEWTONIA_NET_TEST_PLATFORM=6
# against a FAKE_VERIFY relay, which attests the claim), so this drives its
# own relay like nseat_anon.sh. NEWTONIA_NET_TEST_PEER_VERDICT=refuse-name:BOB
# plays a player with BOB on their block list.
#   BOB seats first.
#   ALICE (blocks BOB) seats, hears the room members, and leaves before the
#     game starts; the host frees her seat.
#   CAROL (blocks DAVE, who isn't here yet) seats, the host starts, and
#     CAROL enters the game and tells the platform about both the host and
#     BOB.
#   BOB drops; DAVE takes his seat mid-game. The host's new member list
#     names someone CAROL knows nothing about, so she looks DAVE up at the
#     room server (a short lookup socket), and leaves.
# Prints POLICY-MEMBERS-E2E-OK. See TESTING.md.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
cd "$(dirname "$0")"

PORT="${MEMBERS_RELAY_PORT:-8793}"
export NEWTONIA_SIGNAL_URL="ws://127.0.0.1:$PORT/ws"
. ./lib.sh

# A private wrangler state dir, as nseat_anon.sh does.
WSTATE=$(mktemp -d /tmp/policy_members_wrangler.XXXXXX) || {
  echo "FATAL: could not create private relay state"; exit 1;
}
( cd ../../signal &&
  exec npx wrangler@4 dev --local --port "$PORT" --persist-to "$WSTATE" \
       --var FAKE_VERIFY:1 ) \
  > /tmp/policy_members_wrangler.log 2>&1 &
WPID=$!
trap 'kill_tree "$WPID" 2>/dev/null; rm -rf "$WSTATE"' EXIT
echo "== starting FAKE_VERIFY relay on :$PORT (pid $WPID)"
for _ in $(seq 1 90); do
  curl -s -o /dev/null "http://127.0.0.1:$PORT/" && break
  kill -0 $WPID 2>/dev/null || { echo "relay died:"; cat /tmp/policy_members_wrangler.log; exit 1; }
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

# join_as SLOT NAME [VAR=VAL ...]: launch an "Xbox" pilot and join the room
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

# wait_log FILE PATTERN SECS: poll FILE for PATTERN
wait_log() {
  local i
  for i in $(seq 1 "$3"); do
    grep -aq "$2" "$OUT/$1.log" && return 0
    sleep 1
  done
  return 1
}

echo "== BOB is seated"
join_as 1 BOB
wait_log host "seat 2 filled" 40 || room_fail "BOB WAS NOT SEATED" host

echo "== ALICE, who blocks BOB, is seated and then leaves"
join_as 2 ALICE NEWTONIA_NET_TEST_PEER_VERDICT=refuse-name:BOB
wait_log host "seat 3 filled" 40 || room_fail "ALICE WAS NOT SEATED" host
wait_log ALICE "room member .* identity attested platform=XBOX" 20 ||
  room_fail "ALICE NEVER HEARD WHO BOB IS" ALICE
wait_log ALICE "another player in the room refused by platform policy" 20 ||
  room_fail "ALICE DID NOT LEAVE A ROOM WITH BOB IN IT" ALICE
wait_log host "seat 3 link died pre-start" 30 ||
  room_fail "THE HOST NEVER FREED ALICE'S SEAT" host
grep -aq "bootstrap adopted" "$OUT/ALICE.log" &&
  room_fail "ALICE ENTERED THE GAME" ALICE
grep -aq "refused by platform policy" "$OUT/BOB.log" &&
  room_fail "BOB REFUSED SOMEONE" BOB

echo "== CAROL, who blocks only DAVE, plays with the host and BOB"
join_as 3 CAROL NEWTONIA_NET_TEST_PEER_VERDICT=refuse-name:DAVE
ok=
for _ in $(seq 1 40); do
  [ "$(grep -ac "seat 3 filled" "$OUT/host.log")" -ge 2 ] && { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "CAROL WAS NOT SEATED" host
wait_log CAROL "room members from the host: 1 other joiner" 20 ||
  room_fail "CAROL NEVER HEARD THE ROOM MEMBERS" CAROL
echo "== host starts the game"
xdotool key --window "$HW" Return; sleep 1
wait_log host "starting with 2 peer" 20 || room_fail "THE HOST DID NOT START" host
wait_log CAROL "bootstrap adopted" 30 || room_fail "CAROL NEVER ENTERED" CAROL
wait_log BOB "bootstrap adopted" 30 || room_fail "BOB NEVER ENTERED" BOB
# The platform hears of both players CAROL now plays with (the Xbox fork
# records them as recent players): the host, and BOB.
n=$(grep -ac "platform told peer admitted" "$OUT/CAROL.log")
[ "$n" = 2 ] || room_fail "CAROL TOLD THE PLATFORM OF $n PLAYERS, NOT 2" CAROL

echo "== BOB drops; DAVE takes his seat mid-game"
kill -9 "${ROOM_PIDS[1]}"; wait "${ROOM_PIDS[1]}" 2>/dev/null
ROOM_PIDS[1]=; ROOM_WINS[1]=
wait_log host "player 2 lost" 45 || room_fail "BOB'S LOSS NEVER DETECTED" host
join_as 4 DAVE
wait_log host "player 2 rejoined" 45 || room_fail "DAVE WAS NOT SEATED" host
wait_log CAROL "net: room members from the host: 1 other joiner" 20 ||
  room_fail "CAROL NEVER HEARD THE NEW ROOM MEMBERS" CAROL
wait_log CAROL "net: room lookup for 1 player" 20 ||
  room_fail "CAROL NEVER LOOKED DAVE UP" CAROL
wait_log CAROL "net: room lookup answered" 30 ||
  room_fail "CAROL'S LOOKUP WAS NEVER ANSWERED" CAROL
wait_log CAROL "another player in the room refused by platform policy (refused) - leaving" 20 ||
  room_fail "CAROL DID NOT LEAVE A GAME WITH DAVE IN IT" CAROL
grep -aq "refused by platform policy" "$OUT/DAVE.log" &&
  room_fail "DAVE REFUSED SOMEONE" DAVE

room_alive
room_kill_all
assert_clean "$OUT/host.log" "$OUT/BOB.log" "$OUT/ALICE.log" "$OUT/CAROL.log" \
             "$OUT/DAVE.log"
echo "POLICY-MEMBERS-E2E-OK"
