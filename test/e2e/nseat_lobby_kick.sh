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
#   2. a KICK is not a ban — this is the softer of the two actions the row
#      offers, so a fresh process under the same pilot name is let back in
#      (nseat_kick.sh covers the ban side, mid-game);
#   3. START GAME still works afterwards. The kicked session is handed to a
#      drain that closes it a few hundred ms later, and starting inside that
#      window hands the room to the game with the drain still holding it.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
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
  # Named pilots: identity is what a ban would key on (a jid is per-socket),
  # so the victim needs a name for the rejoin half below to prove anything —
  # a nameless retry could never have been barred in the first place.
  ROOM_PIDS[$i]=$(launch "joiner$i" NEWTONIA_NET_NAME="PILOT$i")
  sleep 4
  ROOM_WINS[$i]=$(new_window_since "$before")
  [ -n "${ROOM_WINS[$i]}" ] || room_fail "NO WINDOW FOR joiner$i" "joiner$i"
  nav_join "${ROOM_WINS[$i]}" "$ROOM_CODE" "joiner$i"
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
# The list holds only the peers (the host has no row here) and rests on the
# START GAME row, which is drawn UNDERNEATH them — so UP walks into the list
# from the bottom and one step lands on the last joiner, seat 3.
xdotool key --window "$HW" w; sleep 0.4
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

echo "== a KICK is not a ban: the same pilot can come back"
# The other half of the pair. A kicked player keeps the room code and the
# seat they lost is open again, so a fresh process under the same name must
# be seated — if this ever starts failing with "refusing banned pilot", the
# two actions have collapsed back into one.
before=$(newtonia_windows)
RETRY_PID=$(launch retry NEWTONIA_NET_NAME=PILOT2)
sleep 4
RETRY_WIN=$(new_window_since "$before")
[ -n "$RETRY_WIN" ] || room_fail "NO WINDOW FOR THE RETRY" retry
nav_join "$RETRY_WIN" "$ROOM_CODE" retry
ok=
for _ in $(seq 1 30); do
  grep -aq "seat 3 filled" <(sed -n '/kicking seat 3/,$p' "$OUT/host.log") &&
    { ok=1; break; }
  sleep 1
done
grep -aq "refusing banned pilot" "$OUT/host.log" &&
  room_fail "A PLAIN KICK BANNED THEM" host
[ -n "$ok" ] || room_fail "KICKED PILOT COULD NOT REJOIN" host
kill "$RETRY_PID" 2>/dev/null
sleep 5   # let the room notice the dead session and free the seat again

echo "== START GAME still works after all that"
xdotool key --window "$HW" Return; sleep 2   # host_sel_ is back at the start row
# The count is deliberately not asserted: the retry above was killed, and
# whether the room has reaped its seat by now is a race this test does not
# care about. That the room STARTS and the survivor gets a world is the point.
ok=
for _ in $(seq 1 30); do
  grep -aq "starting with" "$OUT/host.log" && { ok=1; break; }
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
echo "NSEAT-LOBBY-KICK-OK (kicked seat 3 in the waiting room, rejoin allowed)"
