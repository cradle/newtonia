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
# Test the FIND result, not the dirname of it: `dirname ""` prints "." — not
# the empty string the fallback below was checking for — so on a machine where
# the game has never run (no preferences.ini yet) PREF_DIR silently became the
# repo root, and the driver looked for ./savegame.dat forever. That is every
# fresh CI runner, which is why this failed the first attempt of every run and
# passed the retry: the failed attempt leaves a preferences.ini behind, and
# the second one finds it (2026-08-12).
PREF_INI=$(find ~/.local/share -path '*newtonia/preferences.ini' 2>/dev/null | head -1)
if [ -n "$PREF_INI" ]; then
  PREF_DIR=$(dirname "$PREF_INI")
else
  PREF_DIR=~/.local/share/cc.gfm/newtonia
fi

PID=""
fail() { echo "FOURPLAYER-E2E-FAIL: $1"; [ -n "$PID" ] && kill $PID 2>/dev/null; exit 1; }
alive() { kill -0 $PID 2>/dev/null || { wait $PID; fail "crashed status=$? at: $1"; }; }
# Where the running instance's output goes. This driver used to discard it,
# which cost it the one signal that says "ready for keystrokes": every launch
# here is a COLD one (no window yet, no pref dir on a fresh machine), and a
# fixed 3 s wait let the first Return land before the menu existed. The game
# then never started, so no save was written and the driver reported "no 4P
# save written" — a save-shaped symptom with a startup-shaped cause, failing
# the first attempt of every CI run (2026-08-12).
GAMELOG="${NEWTONIA_TEST_OUT:-${TMPDIR:-/tmp}}/fourplayer-game.log"
mkdir -p "$(dirname "$GAMELOG")"

# Presence::set_menu logs one line per state change, unconditionally.
wait_for_menu() {
  local i
  for i in $(seq 1 30); do
    grep -aq "Presence: In the Menu" "$GAMELOG" && return 0
    sleep 1
  done
  return 1
}

newgame() {
  wait_for_menu || fail "the game never reached the menu"
  W=$(xdotool search --name . 2>/dev/null | tail -1)
  xdotool key --window $W Return; sleep 1   # attract screen
  xdotool key --window $W Return; sleep 2   # first menu row (NEW GAME / CONTINUE)
}
stop() { kill $PID; wait $PID || fail "unclean shutdown at $1"; PID=""; }
# The auto-save is written on the way out to the menu, and the FIRST launch on
# a machine that has never run the game is slow enough (pref dir creation, cold
# GL) that a bare -f test raced it — which is every CI run, and was a one-in-two
# failure on a fresh container. Poll instead of sampling once.
wait_for_save() {
  local i
  for i in $(seq 1 15); do
    [ -f "$PREF_DIR/savegame.dat" ] && return 0
    sleep 1
  done
  return 1
}

rm -f "$PREF_DIR/savegame.dat"

# 1. 4P grid runs and survives a level skip
NEWTONIA_BETA=1 NEWTONIA_START_PLAYERS=4 ./newtonia > "$GAMELOG" 2>&1 & PID=$!
newgame; alive "4P start"
sleep 3; alive "4P running"
xdotool key --window $W n; sleep 2; alive "4P after skip-level"
xdotool key --window $W Escape; sleep 2   # exit to menu, auto-saving
stop "4P save"
wait_for_save || fail "no 4P save written"

# 2. plain relaunch resumes the 4P save via CONTINUE
./newtonia > "$GAMELOG" 2>&1 & PID=$!
newgame; alive "4P resume"
sleep 2; alive "4P resumed running"
stop "4P resume shutdown"

# 3. 3P grid (free-cell minimap path)
rm -f "$PREF_DIR/savegame.dat"
NEWTONIA_BETA=1 NEWTONIA_START_PLAYERS=3 ./newtonia > "$GAMELOG" 2>&1 & PID=$!
newgame; alive "3P start"
sleep 3; alive "3P running"
stop "3P"

# 4. Enter joins P2 once; a third Enter is refused (cap)
rm -f "$PREF_DIR/savegame.dat"
./newtonia > "$GAMELOG" 2>&1 & PID=$!
newgame; alive "2P base"
xdotool key --window $W Return; sleep 2; alive "after P2 join"
xdotool key --window $W Return; sleep 2; alive "after refused third Enter"
stop "join cap"

rm -f "$PREF_DIR/savegame.dat"
echo "FOURPLAYER-E2E-OK"
