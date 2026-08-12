#!/bin/bash
# Host kick (FOURPLAYER.md O3), LOBBY half: a 4-seat room that never fills,
# so the waiting room stays up, and the host removes a seated joiner there.
#
# The lobby kick is a different code path from the mid-game one — the peer is
# still a NetLobby::SeatedPeer, not a GLGame::NetPeer — with its own three
# things to get right:
#   1. the kicked joiner is TOLD (EV_KICKED) while still on the connecting
#      screen: that message arrives in the bootstrap loop, which used to drop
#      everything that was not a snapshot chunk, so the goodbye went unparsed
#      and the joiner just watched the link die;
#   2. the ban survives the kick — a fresh process with the same pilot name
#      is refused the seat it just lost;
#   3. START GAME still works afterwards. The kicked session is handed to a
#      drain that closes it a few hundred ms later, and starting inside that
#      window hands the room to the game with the drain still holding it.
set -u
cd "$(dirname "$0")"
. ./lib.sh

relay_check

# A 4-seat room with only 2 joiners never auto-starts, which is the point:
# the waiting room has to still be on screen for the host to kick from.
export NEWTONIA_NET_TEST_SEATS=4

ROOM_PIDS=(); ROOM_WINS=(); ROOM_CODE=
ROOM_PIDS[0]=$(launch host)
sleep 2
ROOM_WINS[0]=$(newtonia_windows | head -1)
[ -n "${ROOM_WINS[0]}" ] || room_fail "NO HOST WINDOW" host
nav_host "${ROOM_WINS[0]}"
ROOM_CODE=$(host_room_code host)
[ -n "$ROOM_CODE" ] || room_fail "NO ROOM CODE" host
echo "room code: $ROOM_CODE"

for i in 1 2; do
  before=$(newtonia_windows)
  # Named pilots: the ban is identity-keyed (a jid is per-socket), so the
  # victim needs a name for the ban half of this to mean anything.
  ROOM_PIDS[$i]=$(launch "joiner$i" NEWTONIA_NET_NAME="PILOT$i")
  sleep 4
  ROOM_WINS[$i]=$(new_window_since "$before")
  [ -n "${ROOM_WINS[$i]}" ] || room_fail "NO WINDOW FOR joiner$i" "joiner$i"
  nav_join "${ROOM_WINS[$i]}" "$ROOM_CODE"
  ok=
  for _ in $(seq 1 40); do
    grep -aq "seat $((i + 1)) filled" "$OUT/host.log" && { ok=1; break; }
    sleep 1
  done
  [ -n "$ok" ] || room_fail "SEAT $((i + 1)) NEVER FILLED" host
  echo "seat $((i + 1)) filled"
done

echo "== host: pick the seat-3 row in the waiting room and kick it"
HW=${ROOM_WINS[0]}
# The list holds only the peers (the host has no row here) and rests at -1,
# the START GAME row: two steps down lands on joiner2.
xdotool key --window "$HW" s; sleep 0.4
xdotool key --window "$HW" s; sleep 0.4
xdotool key --window "$HW" Return; sleep 0.5   # arm
xdotool key --window "$HW" Return; sleep 1.5   # confirm
ok=
for _ in $(seq 1 20); do
  grep -aq "kicking seat 3" "$OUT/host.log" && { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "HOST NEVER KICKED SEAT 3" host

echo "== the kicked joiner must be told while it waits, not just dropped"
ok=
for _ in $(seq 1 20); do
  grep -aq "removed from the room by the host" "$OUT/joiner2.log" &&
    { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "KICKED JOINER NEVER GOT THE EVENT" joiner2

echo "== and cannot buy the seat back under a fresh process"
before=$(newtonia_windows)
BAN_PID=$(launch banned NEWTONIA_NET_NAME=PILOT2)
sleep 4
BAN_WIN=$(new_window_since "$before")
[ -n "$BAN_WIN" ] || room_fail "NO WINDOW FOR THE BANNED RETRY" banned
nav_join "$BAN_WIN" "$ROOM_CODE"
ok=
for _ in $(seq 1 30); do
  grep -aq "refusing banned pilot" "$OUT/host.log" && { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "BANNED PILOT WAS NOT REFUSED" host
kill "$BAN_PID" 2>/dev/null

echo "== START GAME still works with the survivor"
xdotool key --window "$HW" Return; sleep 2   # host_sel_ is back at the start row
ok=
for _ in $(seq 1 30); do
  grep -aq "starting with 1 peer" "$OUT/host.log" && { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "ROOM NEVER STARTED AFTER THE KICK" host
ok=
for _ in $(seq 1 30); do
  grep -aq "bootstrap adopted" "$OUT/joiner1.log" && { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "SURVIVOR NEVER BOOTSTRAPPED" joiner1
sleep 3
room_alive
assert_clean "$OUT/host.log" "$OUT/joiner1.log"

room_kill_all
echo "NSEAT-LOBBY-KICK-OK (kicked seat 3 in the waiting room, started with 1 peer)"
