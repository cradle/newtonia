#!/bin/bash
# The host waiting room answers a MOUSE (FOURPLAYER.md O3). The screen is
# drawn as a text list, and off touch the only tap band it had was the
# undrawn touch one — so "ENTER - START GAME" read as a button and answered
# no click at all, while the finger-sized exit strip below it swallowed
# every click in the bottom fifth of the window and left the room.
#
# Asserts, in the order the failures bit:
#   1. a click low on the screen, well under the RETURN TO MENU text, does
#      NOT leave (the room is still up afterwards);
#   2. a click on the drawn RETURN TO MENU text still does;
#   3. a click on the "ENTER - START GAME" row starts the game.
#
# A 4-seat room with one joiner never auto-starts, which is what keeps the
# waiting room on screen to click at.
set -u
cd "$(dirname "$0")"
. ./lib.sh

relay_check
export NEWTONIA_NET_TEST_SEATS=4

# The list's geometry, mirrored from the draw (net_lobby.cpp, desktop
# waiting room): origin y=20, 52 per row, size 18. Rows are ROOM CODE,
# COPIED TO CLIPBOARD, "", the count line, one per peer, "", then START.
# vy = (1 - 2*ny) * 600  =>  ny = (1 - vy/600) / 2, and a row's mid-line
# sits a glyph size below its anchor.
row_ny() { awk -v i="$1" 'BEGIN { vy = 20 - i * 52 - 18; print (1 - vy / 600) / 2 }'; }

click() {  # click WINDOW NY
  local w=$1 ny=$2 wh ww
  # Raise first: every instance opens at (0,0) at the same size and there is
  # no window manager, so the pointer hits whichever window is on top —
  # which is the JOINER, launched last. Without this the clicks land on
  # another process and the assertions pass for the wrong reason (they did).
  xdotool windowraise "$w"; sleep 0.5
  wh=$(xdotool getwindowgeometry --shell "$w" | sed -n 's/^HEIGHT=//p')
  ww=$(xdotool getwindowgeometry --shell "$w" | sed -n 's/^WIDTH=//p')
  xdotool mousemove --window "$w" $((ww / 2)) \
          "$(awk -v h="$wh" -v n="$ny" 'BEGIN { printf "%d", h * n }')" \
          click 1
  sleep 1
}

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

before=$(newtonia_windows)
ROOM_PIDS[1]=$(launch joiner1 NEWTONIA_NET_NAME=PILOT1)
sleep 4
ROOM_WINS[1]=$(new_window_since "$before")
[ -n "${ROOM_WINS[1]}" ] || room_fail "NO WINDOW FOR joiner1" joiner1
nav_join "${ROOM_WINS[1]}" "$ROOM_CODE" joiner1
ok=
for _ in $(seq 1 40); do
  grep -aq "seat 2 filled" "$OUT/host.log" && { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "SEAT 2 NEVER FILLED" host

echo "== a click BELOW the exit text must not leave the room"
click "$HW" 0.95
grep -aq "starting with" "$OUT/host.log" &&
  room_fail "LOW CLICK STARTED THE GAME" host
# Still hosting: the code line is only drawn by the lobby, and leaving
# would have closed the room at the relay.
xdotool search --name Newtonia | grep -q "^$HW$" ||
  room_fail "HOST WINDOW GONE AFTER THE LOW CLICK" host
grep -aq "room closed\|leaving" "$OUT/host.log" &&
  room_fail "LOW CLICK LEFT THE LOBBY" host

echo "== a click ON the START row starts the game"
click "$HW" "$(row_ny 6)"
ok=
for _ in $(seq 1 20); do
  grep -aq "starting with 1 peer" "$OUT/host.log" && { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "CLICKING START DID NOTHING" host
ok=
for _ in $(seq 1 30); do
  grep -aq "bootstrap adopted" "$OUT/joiner1.log" && { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "JOINER NEVER BOOTSTRAPPED" joiner1
room_alive

room_kill_all
echo "NSEAT-LOBBY-MOUSE-OK (low click inert, START row clickable)"
