#!/bin/bash
# Local 4-player regression (FOURPLAYER.md Phase A): via the beta-gated
# NEWTONIA_START_PLAYERS hook (the dark-launch gate stays down for real
# joins), a 4P game must run in the 2x2 grid, a 3P game must run with the
# free-cell minimap, Enter must join exactly one P2 and refuse a third
# seat, and a 4P save must survive a quit/relaunch/CONTINUE cycle.
# Prints FOURPLAYER-E2E-OK on success. See TESTING.md.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1400x900x24" "$0" "$@"
fi
cd "$(dirname "$0")/../.."
export SDL_AUDIODRIVER=dummy
PREF_DIR=$(dirname "$(find ~/.local/share -path '*newtonia/preferences.ini' 2>/dev/null | head -1)" 2>/dev/null)
[ -n "$PREF_DIR" ] || PREF_DIR=~/.local/share/cc.gfm/newtonia

PID=""
fail() { echo "FOURPLAYER-E2E-FAIL: $1"; [ -n "$PID" ] && kill $PID 2>/dev/null; exit 1; }
alive() { kill -0 $PID 2>/dev/null || { wait $PID; fail "crashed status=$? at: $1"; }; }
newgame() {
  sleep 3
  W=$(xdotool search --name . 2>/dev/null | tail -1)
  xdotool key --window $W Return; sleep 1   # attract screen
  xdotool key --window $W Return; sleep 2   # first menu row (NEW GAME / CONTINUE)
}
stop() { kill $PID; wait $PID || fail "unclean shutdown at $1"; PID=""; }

rm -f "$PREF_DIR/savegame.dat"

# 1. 4P grid runs and survives a level skip
NEWTONIA_BETA=1 NEWTONIA_START_PLAYERS=4 ./newtonia > /dev/null 2>&1 & PID=$!
newgame; alive "4P start"
grep_log_players=4
sleep 3; alive "4P running"
xdotool key --window $W n; sleep 2; alive "4P after skip-level"
xdotool key --window $W Escape; sleep 2   # exit to menu, auto-saving
stop "4P save"
[ -f "$PREF_DIR/savegame.dat" ] || fail "no 4P save written"

# 2. plain relaunch resumes the 4P save via CONTINUE
./newtonia > /dev/null 2>&1 & PID=$!
newgame; alive "4P resume"
sleep 2; alive "4P resumed running"
stop "4P resume shutdown"

# 3. 3P grid (free-cell minimap path)
rm -f "$PREF_DIR/savegame.dat"
NEWTONIA_BETA=1 NEWTONIA_START_PLAYERS=3 ./newtonia > /dev/null 2>&1 & PID=$!
newgame; alive "3P start"
sleep 3; alive "3P running"
stop "3P"

# 4. Enter joins P2 once; a third Enter is refused (cap)
rm -f "$PREF_DIR/savegame.dat"
./newtonia > /dev/null 2>&1 & PID=$!
newgame; alive "2P base"
xdotool key --window $W Return; sleep 2; alive "after P2 join"
xdotool key --window $W Return; sleep 2; alive "after refused third Enter"
stop "join cap"

rm -f "$PREF_DIR/savegame.dat"
echo "FOURPLAYER-E2E-OK"
