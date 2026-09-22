#!/bin/bash
# The first-time pilot's start screen + tutorial (tutorial.h): a fresh
# install opens on the TUTORIAL / PLAY / OPTIONS start screen; TUTORIAL runs
# the walk-through on an empty field; a real thrust run reaches the beacon
# (twice — once per camera, through the Halo-style keep/switch prompt);
# the beta skip key walks the remaining steps; a fire press at the end
# starts the first real game; and the latch (tutorial_done=1) flips the
# next launch onto the full menu. Prints TUTORIAL-E2E-OK on success. See
# TESTING.md.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
# lib.sh reads every driver's pilot as tutorial-done (the classic row
# layout); this driver is about the real fresh-install path.
unset NEWTONIA_TUTORIAL

INI="$XDG_DATA_HOME/cc.gfm/newtonia/preferences.ini"
LOG="$OUT/tutorial.log"

# wait_log PATTERN SECS: poll the log for a line (the step machine logs
# every transition); exits the driver on timeout with the log's tail.
wait_log() {
  local i count required=${3:-1}
  for i in $(seq 1 $(( $2 * 4 ))); do
    count=$(grep -ac "$1" "$LOG" || true)
    [ "$count" -ge "$required" ] && return 0
    sleep 0.25
  done
  echo "TIMEOUT waiting for: $1"; tail -20 "$LOG"; exit 1
}
fail() { echo "$1"; tail -20 "$LOG"; kill $P 2>/dev/null; exit 1; }

P=$(launch tutorial NEWTONIA_BETA=1)
trap 'kill "$P" 2>/dev/null || true' EXIT
sleep 3
W=$(newtonia_windows | tail -1)
[ -n "$W" ] || fail "NO WINDOW"

# A fresh install writes its INI on the spot with the tutorial pending.
grep -q '^tutorial_done=0' "$INI" || fail "fresh install did not arm the tutorial"

echo "== start screen"
key $W Return; sleep 1            # attract -> the start screen
shot $W start_screen
key $W Return                     # row 0: TUTORIAL
wait_log "tutorial: started" 5
alive $P tutorial
sleep 1.5                         # the first-life READY window
shot $W tutorial_launch

echo "== launch"
key $W space
wait_log "tutorial: step TURN" 5

echo "== turn (skipped), thrust to the beacon for real"
key $W n
wait_log "tutorial: step THRUST" 5
xdotool keydown --window $W w; sleep 5; xdotool keyup --window $W w
wait_log "tutorial: step CAMERA" 8
alive $P tutorial
shot $W camera_prompt

echo "== switch camera: the calibration re-runs under FIXED"
key $W s; key $W Return
wait_log "tutorial: camera switched to FIXED" 5
grep -q '^p1_rotate_view=0' "$INI" || fail "camera switch not persisted"
key $W n                          # TURN again, skipped
wait_log "tutorial: step THRUST" 5 2
xdotool keydown --window $W w; sleep 5; xdotool keyup --window $W w
wait_log "tutorial: step CAMERA" 8 2
key $W Return                     # KEEP FIXED CAMERA
wait_log "tutorial: step FIRE" 5
shot $W fire_step

echo "== the rest, skipped"
key $W n; wait_log "tutorial: step BOOST" 5
key $W n; wait_log "tutorial: step SECONDARY" 5
key $W n; wait_log "tutorial: step WRAP" 5
alive $P tutorial
grep -q '^tutorial_done=1' "$INI" || fail "wrap-up did not latch tutorial_done"
key $W space; wait_log "tutorial: step DONE" 5
shot $W tutorial_done
# The tutorial banked nothing and left no save behind — checked HERE,
# before the real game starts (which writes both files on its own).
[ ! -e "$XDG_DATA_HOME/cc.gfm/newtonia/savegame.dat" ] || fail "tutorial wrote a save"
[ ! -e "$XDG_DATA_HOME/cc.gfm/newtonia/stats.dat" ] || fail "tutorial wrote stats"
[ ! -e "$XDG_DATA_HOME/cc.gfm/newtonia/highscore.dat" ] || fail "tutorial wrote a high score"
grep -aq "replay: recording started" "$LOG" && fail "tutorial recorded a replay"

echo "== fire starts the first real game"
key $W space
wait_log "tutorial: complete" 5
sleep 3
alive $P tutorial
shot $W first_game
grep -aq "replay: recording started" "$LOG" || fail "the first game did not record"

echo "== back to the menu: the full layout now"
key $W Escape; sleep 2
alive $P tutorial
shot $W menu_after
kill $P; wait $P 2>/dev/null
grep -aq "Segmentation\|Assertion" "$LOG" && fail "crash in log"
echo TUTORIAL-E2E-OK
