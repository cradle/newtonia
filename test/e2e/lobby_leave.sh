#!/bin/bash
# A seated joiner backing out of the waiting room frees its seat AT ONCE.
#
# The joiner's lobby used to just delete its session on the way to the menu,
# so the host only learned of the leave when the link finally failed — half a
# minute or more on a relayed link — and its roster kept the seat (and the
# count line its old count) the whole time (field, 2026-10-27). The joiner
# now sends EV_BYE and holds its teardown a moment so the goodbye reaches the
# wire; the host frees the seat when it reads it.
#
# A 4-seat room with two joiners, so it never auto-starts: seat 3 presses Esc,
# and the host must log the leave within a few seconds — well inside the time
# a dead link takes — while seat 2 stays seated and START still works.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
cd "$(dirname "$0")"
. ./lib.sh

relay_check

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
  ROOM_PIDS[$i]=$(launch "joiner$i")
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

echo "== seat 3 backs out of the waiting room"
xdotool key --window "${ROOM_WINS[2]}" Escape
ok=
for _ in $(seq 1 8); do
  grep -aq "seat 3 left pre-start" "$OUT/host.log" && { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "HOST KEPT THE SEAT OF A PLAYER WHO LEFT" host
grep -aq "leaving the waiting room - told the host" "$OUT/joiner2.log" ||
  room_fail "JOINER NEVER SAID GOODBYE" joiner2
grep -aq "seat 2 left pre-start\|seat 2 link died pre-start" "$OUT/host.log" &&
  room_fail "THE WRONG SEAT WAS FREED" host
kill -0 "${ROOM_PIDS[2]}" 2>/dev/null || room_fail "JOINER DIED LEAVING" joiner2
# Back at the menu, the leaver is no longer part of the room: drop its
# process from the liveness set before the start below.
kill "${ROOM_PIDS[2]}" 2>/dev/null
unset 'ROOM_PIDS[2]' 'ROOM_WINS[2]'

echo "== START GAME with the one who stayed"
xdotool key --window "${ROOM_WINS[0]}" Return; sleep 2
ok=
for _ in $(seq 1 30); do
  grep -aq "bootstrap adopted" "$OUT/joiner1.log" && { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "THE JOINER WHO STAYED NEVER BOOTSTRAPPED" joiner1
sleep 3
room_alive
assert_clean "$OUT/host.log" "$OUT/joiner1.log"

room_kill_all
echo "LOBBY-LEAVE-OK (a joiner who backed out was freed at once)"
