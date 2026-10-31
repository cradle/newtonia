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
#     game starts; the host frees her seat. BOB was there first.
#   CAROL (blocks DAVE and ERIN, neither here yet) seats. BOB drops before
#     the start, and DAVE takes the free seat: DAVE came after CAROL, so
#     CAROL asks the host to turn him away (MSG_MEMBER_REFUSED) and stays.
#   BOB comes back, the host starts, and CAROL enters the game and tells
#     the platform about both the host and BOB.
#   BOB drops; ERIN takes his seat mid-game. The rejoin door holds her
#     before seating her (fork NETPLAY_XBOX.md 7e), and its member list
#     names someone CAROL knows nothing about, so CAROL looks ERIN up at
#     the room server (a short lookup socket), and the host turns ERIN away
#     from the door: she never enters the game.
#   DAVE tries again and is refused at the door while CAROL is still here.
#   FRANK (blocks CAROL) takes the free seat: the door holds him, he leaves
#     from the door, and he is never seated either.
#   HANK (blocks the host) takes the free seat: he leaves from the door on
#     the host's verdict alone, with no snapshot to wait for.
#   GINA, whose own platform check takes 2.5 s, takes the free seat: the
#     door holds her until she and CAROL have both answered, then seats her.
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
# Named, so a pilot's block list can name the host (HANK below).
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

# join_as SLOT LOG [VAR=VAL ...]: launch an "Xbox" pilot and join the room.
# The pilot's name is LOG up to any digit (DAVE2 is DAVE again).
join_as() {
  local slot=$1 log=$2 name before w
  shift 2
  name=${log%%[0-9]*}
  before=$(newtonia_windows)
  ROOM_PIDS[$slot]=$(launch "$log" $XBOX NEWTONIA_NET_NAME="$name" \
                     NEWTONIA_NET_TEST_CRED="XUID-$name" "$@")
  sleep 4
  w=$(new_window_since "$before")
  [ -n "$w" ] || room_fail "NO WINDOW FOR $log" "$log"
  ROOM_WINS[$slot]=$w
  nav_join "$w" "$ROOM_CODE" "$log"
}

# drop SLOT: kill a pilot outright, and wait for its window to go (X can
# hand the next instance the same window id, which would hide it from
# new_window_since)
drop() {
  local w=${ROOM_WINS[$1]} i
  kill -9 "${ROOM_PIDS[$1]}"; wait "${ROOM_PIDS[$1]}" 2>/dev/null
  ROOM_PIDS[$1]=; ROOM_WINS[$1]=
  for i in $(seq 1 20); do
    newtonia_windows | grep -q "^$w$" || return 0
    sleep 0.5
  done
}

# count_log FILE PATTERN: matching lines so far
count_log() { grep -ac "$2" "$OUT/$1.log"; }

# wait_count FILE PATTERN N SECS: poll until at least N lines match
wait_count() {
  local i
  for i in $(seq 1 "$4"); do
    [ "$(count_log "$1" "$2")" -ge "$3" ] && return 0
    sleep 1
  done
  return 1
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

echo "== CAROL, who blocks DAVE and ERIN, is seated"
join_as 3 CAROL NEWTONIA_NET_TEST_PEER_VERDICT=refuse-name:DAVE,ERIN
wait_count host "seat 3 filled" 2 40 || room_fail "CAROL WAS NOT SEATED" host
wait_log CAROL "room members from the host: 1 other joiner" 20 ||
  room_fail "CAROL NEVER HEARD THE ROOM MEMBERS" CAROL

echo "== BOB drops before the start; DAVE takes the free seat and is turned away"
drop 1
wait_log host "seat 2 link died pre-start" 45 || room_fail "BOB'S SEAT NOT FREED" host
join_as 4 DAVE
wait_count host "seat 2 filled" 2 40 || room_fail "DAVE WAS NOT SEATED" host
wait_log CAROL "asking the host to turn away a newer player" 20 ||
  room_fail "CAROL NEVER ASKED TO TURN DAVE AWAY" CAROL
wait_log host "waiting room: turning away seat 2" 20 ||
  room_fail "THE HOST DID NOT TURN DAVE AWAY" host
wait_log DAVE "turned away" 20 || room_fail "DAVE WAS NEVER TOLD" DAVE
grep -aq "refused by platform policy" "$OUT/CAROL.log" &&
  room_fail "CAROL LEFT INSTEAD OF DAVE" CAROL
drop 4

echo "== BOB comes back; the host starts the game"
join_as 1 BOB2
wait_count host "seat 2 filled" 3 40 || room_fail "BOB WAS NOT SEATED AGAIN" host
wait_count CAROL "room members from the host: 1 other joiner" 3 20 ||
  room_fail "CAROL NEVER HEARD BOB WAS BACK" CAROL
xdotool key --window "$HW" Return; sleep 1
wait_log host "starting with 2 peer" 20 || room_fail "THE HOST DID NOT START" host
wait_log CAROL "bootstrap adopted" 30 || room_fail "CAROL NEVER ENTERED" CAROL
wait_log BOB2 "bootstrap adopted" 30 || room_fail "BOB NEVER ENTERED" BOB2
# The platform hears of both players CAROL now plays with (the Xbox fork
# records them as recent players): the host, and BOB.
n=$(count_log CAROL "platform told peer admitted")
[ "$n" = 2 ] || room_fail "CAROL TOLD THE PLATFORM OF $n PLAYERS, NOT 2" CAROL

echo "== BOB drops; ERIN takes his seat mid-game and is turned away"
drop 1
wait_log host "player 2 lost" 45 || room_fail "BOB'S LOSS NEVER DETECTED" host
join_as 4 ERIN
wait_log host "holding the newcomer for seat 2 at the door" 45 ||
  room_fail "THE DOOR DID NOT HOLD ERIN" host
wait_log CAROL "net: room lookup for 1 player" 20 ||
  room_fail "CAROL NEVER LOOKED ERIN UP" CAROL
# No wait on the lookup's "answered" line: ERIN's identity frame lands
# ahead of it, and the verdict can act on it in between (CI, run
# 40321607957).
wait_log CAROL "net: asking the host to turn away a newer player" 20 ||
  room_fail "CAROL NEVER ASKED TO TURN ERIN AWAY MID-GAME" CAROL
wait_log host "turning away the newcomer held at the door for seat 2" 20 ||
  room_fail "THE HOST DID NOT TURN ERIN AWAY MID-GAME" host
wait_log ERIN "turned away" 20 || room_fail "ERIN WAS NEVER TOLD" ERIN
# Turned away from the door, never seated: no RECONNECTED, no game.
grep -aq "player 2 rejoined" "$OUT/host.log" &&
  room_fail "ERIN WAS SEATED BEFORE SHE WAS TURNED AWAY" host
grep -aq "bootstrap adopted" "$OUT/ERIN.log" &&
  room_fail "ERIN ENTERED THE GAME" ERIN
grep -aq "refused by platform policy" "$OUT/CAROL.log" &&
  room_fail "CAROL LEFT INSTEAD OF ERIN" CAROL
grep -aq "refused by platform policy" "$OUT/ERIN.log" &&
  room_fail "ERIN REFUSED SOMEONE" ERIN
drop 4

echo "== DAVE tries again and is refused at the door while CAROL plays"
join_as 4 DAVE2
wait_log host "refusing a player someone in the room can't play with" 45 ||
  room_fail "THE DOOR LET DAVE BACK IN" host
wait_log DAVE2 "host refused this connection (reason 2)" 30 ||
  room_fail "DAVE WAS NEVER REFUSED" DAVE2
drop 4

echo "== FRANK, who blocks CAROL, takes the free seat and leaves from the door"
holds=$(count_log host "holding the newcomer for seat 2 at the door")
join_as 4 FRANK NEWTONIA_NET_TEST_PEER_VERDICT=refuse-name:CAROL
wait_count host "holding the newcomer for seat 2 at the door" $((holds + 1)) 45 ||
  room_fail "THE DOOR DID NOT HOLD FRANK" host
wait_log FRANK "another player in the room refused by platform policy" 20 ||
  room_fail "FRANK DID NOT LEAVE A ROOM WITH CAROL IN IT" FRANK
wait_log host "the newcomer for seat 2 left the door" 30 ||
  room_fail "THE HOST NEVER SAW FRANK LEAVE THE DOOR" host
grep -aq "player 2 rejoined" "$OUT/host.log" &&
  room_fail "FRANK WAS SEATED BEFORE HE LEFT" host
grep -aq "bootstrap adopted" "$OUT/FRANK.log" &&
  room_fail "FRANK ENTERED THE GAME" FRANK
grep -aq "refused by platform policy" "$OUT/CAROL.log" &&
  room_fail "CAROL LEFT INSTEAD OF FRANK" CAROL
drop 4

echo "== HANK, who blocks the host, leaves from the door"
holds=$(count_log host "holding the newcomer for seat 2 at the door")
lefts=$(count_log host "the newcomer for seat 2 left the door")
join_as 4 HANK NEWTONIA_NET_TEST_PEER_VERDICT=refuse-name:HOST
wait_count host "holding the newcomer for seat 2 at the door" $((holds + 1)) 45 ||
  room_fail "THE DOOR DID NOT HOLD HANK" host
wait_log HANK "host refused by platform policy (refused)" 20 ||
  room_fail "HANK DID NOT LEAVE ON THE HOST'S VERDICT" HANK
wait_count host "the newcomer for seat 2 left the door" $((lefts + 1)) 20 ||
  room_fail "THE HOST NEVER SAW HANK LEAVE THE DOOR" host
grep -aq "unanswered after" "$OUT/host.log" &&
  room_fail "THE DOOR TIMED OUT ON HANK INSTEAD OF SEEING HIM LEAVE" host
grep -aq "player 2 rejoined" "$OUT/host.log" &&
  room_fail "HANK WAS SEATED BEFORE HE LEFT" host
grep -aq "bootstrap adopted" "$OUT/HANK.log" &&
  room_fail "HANK ENTERED THE GAME" HANK
drop 4

echo "== GINA, whose own check is slow, is held at the door and then seated"
holds=$(count_log host "holding the newcomer for seat 2 at the door")
join_as 4 GINA NEWTONIA_NET_TEST_PEER_VERDICT=slow:2500
wait_count host "holding the newcomer for seat 2 at the door" $((holds + 1)) 45 ||
  room_fail "THE DOOR DID NOT HOLD GINA" host
wait_log host "player 2 rejoined" 30 || room_fail "GINA WAS NOT SEATED" host
wait_log GINA "telling the host we allow everyone in the room" 5 ||
  room_fail "GINA NEVER ANSWERED THE DOOR" GINA
wait_log CAROL "telling the host a newer player is allowed" 5 ||
  room_fail "CAROL NEVER ANSWERED FOR GINA" CAROL
# Seated only once both had answered: after GINA's 2.5 s check.
ms=$(grep -a "door checks for seat 2 done after" "$OUT/host.log" | tail -1 |
     sed 's/.*done after \([0-9]*\) ms.*/\1/')
[ -n "$ms" ] && [ "$ms" -ge 2000 ] ||
  room_fail "GINA WAS SEATED BEFORE HER CHECK FINISHED (${ms:-no line})" host
grep -aq "unanswered after" "$OUT/host.log" &&
  room_fail "THE DOOR TIMED OUT INSTEAD OF HEARING THE ANSWERS" host
wait_log GINA "bootstrap adopted" 30 || room_fail "GINA NEVER ENTERED" GINA
grep -aq "refused by platform policy" "$OUT/GINA.log" &&
  room_fail "GINA REFUSED SOMEONE" GINA

room_alive
room_kill_all
assert_clean "$OUT/host.log" "$OUT/BOB.log" "$OUT/ALICE.log" "$OUT/CAROL.log" \
             "$OUT/DAVE.log" "$OUT/BOB2.log" "$OUT/ERIN.log" "$OUT/DAVE2.log" \
             "$OUT/FRANK.log" "$OUT/HANK.log" "$OUT/GINA.log"
echo "POLICY-MEMBERS-E2E-OK"
