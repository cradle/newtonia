#!/bin/bash
# REPLAY.md R3 exit criteria, headless (keyboard; the controller shares the
# nav translator and touch reuses the options-row band geometry):
#   record a run, then from the menu: REPLAYS row (shown only once a .nrp
#   exists) -> list screen -> select CURRENT RUN -> playback starts -> Esc
#   returns to the menu -> reopen the list -> Esc backs out of it.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"

FAIL=0
fail() { echo "FAIL: $*"; FAIL=1; }

export XDG_DATA_HOME="$OUT/xdg-menu"
RDIR="$XDG_DATA_HOME/cc.gfm/newtonia/replays"

launch_game() { "$ROOT/newtonia" > "$OUT/$1.log" 2>&1 & echo $!; }
win() { xdotool search --name Newtonia | tail -1; }
stop_hard() { kill -9 "$1" 2>/dev/null; wait "$1" 2>/dev/null; sleep 0.5; }

# The current game's pid rides $P throughout; kill it on ANY exit so a
# failed assertion never leaks a live game under Xvfb (they linger as
# zombies whose focus-loss flushes corrupt later runs' assertions).
P=""
trap 'kill -9 $P 2>/dev/null' EXIT

echo "== record a short run"
P=$(launch_game menurec); sleep 2; W=$(win)
key "$W" Return; sleep 0.5; key "$W" Return
sleep 0.5; key "$W" space; sleep 4
key "$W" Escape; sleep 2
stop_hard $P
[ -f "$RDIR/current.nrp" ] || { fail "no current.nrp recorded"; }

echo "== menu -> REPLAYS -> play -> Esc -> menu -> REPLAYS -> Esc"
P=$(launch_game menu); sleep 2; W=$(win)
key "$W" Return; sleep 0.5                       # attract
# Rows with a save + a replay: CONTINUE, NEW GAME, [ONLINE], OPTIONS, REPLAYS
# — REPLAYS is last; overshoot the cursor (it clamps at the bottom).
for i in $(seq 1 6); do key "$W" s; done
shot "$W" menu-rows
key "$W" Return; sleep 1                         # open the list
shot "$W" replays-list
key "$W" Return; sleep 2                         # select CURRENT RUN
grep -q "replay: playback started" "$OUT/menu.log" \
  || fail "selecting the row did not start playback"
shot "$W" menu-playback
key "$W" Escape; sleep 2                         # exit playback -> menu
kill -0 $P 2>/dev/null || fail "died leaving playback"
key "$W" Return; sleep 0.5                       # attract again
for i in $(seq 1 6); do key "$W" s; done
key "$W" Return; sleep 1                         # reopen the list
key "$W" Escape; sleep 1                         # back out of the list
kill -0 $P 2>/dev/null || fail "died backing out of the list"
shot "$W" menu-after
# Both playback starts logged (one per list visit would be 1; we played once)
N=$(grep -c "replay: playback started" "$OUT/menu.log")
[ "$N" = 1 ] || fail "expected exactly 1 playback start, got $N"
stop_hard $P

echo
if [ "$FAIL" = 0 ]; then echo "REPLAY-R3-OK"; else echo "REPLAY-R3-FAIL"; exit 1; fi
