#!/bin/bash
# REPLAY.md R1 exit criteria, headless (no relay needed — replays are solo):
#   S1  abandon keeps a resumable current.nrp; CONTINUE appends to the SAME
#       file (one run_id, seam keyframe, slots continue); NEW GAME rotates
#       the old run into recent.nrp and (clean, non-cheated) promotes best
#   S2  a higher-scoring clean run replaces best on rotation
#   S3  a cheat-flagged run lands in recent but never best
#   S4  game over patches the header (ENDED, score/gen/duration) and
#       rotates current -> recent; the savegame is deleted
#   S5  a crashed run (stale header) rotates into recent with NO best check
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"

CHECK="$ROOT/test/e2e/replay_check.py"
FAIL=0
fail() { echo "FAIL: $*"; FAIL=1; }

# Per-scenario pref-path isolation. Sets SAVE/RDIR for the assertions.
use_home() {
  export XDG_DATA_HOME="$OUT/xdg-$1"
  SAVE="$XDG_DATA_HOME/cc.gfm/newtonia/savegame.dat"
  RDIR="$XDG_DATA_HOME/cc.gfm/newtonia/replays"
}

# field FILE NAME -> value from replay_check.py
field() { python3 "$CHECK" "$1" | sed -n "s/^$2=//p"; }

# save_run_id -> hex of the savegame's trailing run_id (v17 appends it last)
save_run_id() { tail -c 8 "$SAVE" | od -An -tx1 | tr -d ' \n'; }
# header run_id, byte-reversed to match od's little-endian byte order
file_run_id() { dd if="$1" bs=1 skip=32 count=8 2>/dev/null | od -An -tx1 | tr -d ' \n'; }

launch_game() { "$ROOT/newtonia" > "$OUT/$1.log" 2>&1 & echo $!; }
win() { xdotool search --name Newtonia | tail -1; }
# kill -9: SDL converts a plain TERM into an SDL_QUIT event the GLUT loop
# never drains, so TERM'd games linger as zombies simulating in the
# background (their focus-loss flushes then corrupt later assertions).
# Only ever called once the recorder is already finalized (menu/at rest).
stop_at_menu() { kill -9 "$1" 2>/dev/null; wait "$1" 2>/dev/null; sleep 0.5; }

# menu_new_game WINDOW HAS_SAVE: attract -> NEW GAME (+ YES on the confirm)
menu_new_game() {
  key "$1" Return
  if [ "$2" = "1" ]; then
    key "$1" s; key "$1" Return   # down to NEW GAME (CONTINUE is first)
    key "$1" w; key "$1" Return   # confirm: YES sits above the default NO
  else
    key "$1" Return               # no save: NEW GAME is the first row
  fi
  sleep 0.5
  key "$1" space                  # fire: spawn out of the initial countdown
}

echo "===== S1: abandon / resume-append / rotation + first best ====="
use_home s1
P=$(launch_game s1a); sleep 2; W=$(win)
menu_new_game "$W" 0
sleep 4
key "$W" Escape; sleep 2                       # abandon -> menu (finalize)
alive $P s1a

[ -f "$RDIR/current.nrp" ] || fail "S1: current.nrp missing after abandon"
[ -f "$RDIR/recent.nrp" ] && fail "S1: recent.nrp should not exist yet"
[ "$(field "$RDIR/current.nrp" clean)" = 1 ] || fail "S1: abandon should patch header CLEAN"
[ "$(field "$RDIR/current.nrp" ended)" = 0 ] || fail "S1: abandoned run must not be ENDED"
D1=$(field "$RDIR/current.nrp" deltas); [ "$D1" -gt 0 ] || fail "S1: no deltas recorded"
K1=$(field "$RDIR/current.nrp" keyframes); [ "$K1" -ge 1 ] || fail "S1: no keyframe"
S1_SLOT=$(field "$RDIR/current.nrp" last_slot)
RID1=$(file_run_id "$RDIR/current.nrp")
[ -f "$SAVE" ] || fail "S1: no savegame after abandon"
[ "$(save_run_id)" = "$RID1" ] || fail "S1: save run_id != replay run_id"

echo "-- CONTINUE appends to the same file"
key "$W" Return; sleep 0.5; key "$W" Return    # attract -> CONTINUE
sleep 3
key "$W" Escape; sleep 2
alive $P s1a
[ -f "$RDIR/recent.nrp" ] && fail "S1: resume must not rotate"
[ "$(file_run_id "$RDIR/current.nrp")" = "$RID1" ] || fail "S1: run_id changed on resume"
K2=$(field "$RDIR/current.nrp" keyframes)
[ "$K2" -gt "$K1" ] || fail "S1: no seam keyframe on resume ($K1 -> $K2)"
S2_SLOT=$(field "$RDIR/current.nrp" last_slot)
[ "$S2_SLOT" -gt "$S1_SLOT" ] || fail "S1: slots did not continue ($S1_SLOT -> $S2_SLOT)"
grep -q "replay: resuming recording" "$OUT/s1a.log" || fail "S1: no resume log line"

echo "-- NEW GAME rotates the old run into recent (+best)"
menu_new_game "$W" 1
sleep 2
key "$W" Escape; sleep 2
stop_at_menu $P
[ -f "$RDIR/recent.nrp" ] || fail "S1: NEW GAME did not rotate into recent"
[ "$(file_run_id "$RDIR/recent.nrp")" = "$RID1" ] || fail "S1: recent is not the old run"
[ -f "$RDIR/best.nrp" ] || fail "S1: clean non-cheated run should promote first best"
[ "$(file_run_id "$RDIR/current.nrp")" = "$RID1" ] && fail "S1: new game reused old run_id"

echo "===== S2: higher-scoring run replaces best ====="
# Same home: CONTINUE the third run, score by spraying shots, clean-abandon,
# then NEW GAME to rotate + promote.
P=$(launch_game s2); sleep 2; W=$(win)
key "$W" Return; sleep 0.5; key "$W" Return    # attract -> CONTINUE
sleep 1; key "$W" space
# The starting gun is semi-automatic — a held key fires ONE bullet — so
# tap fire while spinning to spray the field.
xdotool keydown --window "$W" d
for i in $(seq 1 100); do xdotool key --window "$W" space; sleep 0.1; done
xdotool keyup --window "$W" d
sleep 1
key "$W" Escape; sleep 2
menu_new_game "$W" 1
sleep 2
key "$W" Escape; sleep 2                       # abandon the just-started run
stop_at_menu $P
BSCORE=$(field "$RDIR/best.nrp" score)
BRID=$(file_run_id "$RDIR/best.nrp")
RSCORE=$(field "$RDIR/recent.nrp" score)
[ "$RSCORE" -gt 0 ] || fail "S2: scoring run scored 0 (shots missed?)"
[ "$BSCORE" = "$RSCORE" ] || fail "S2: best ($BSCORE) != rotated run's score ($RSCORE)"
[ "$BRID" = "$(file_run_id "$RDIR/recent.nrp")" ] || fail "S2: best is not the scoring run"

echo "===== S3: cheat run -> recent, never best ====="
use_home s3
P=$(launch_game s3); sleep 2; W=$(win)
menu_new_game "$W" 0
sleep 2
key "$W" n; sleep 3                            # skip level: cheat + rollover flush
key "$W" Escape; sleep 2
menu_new_game "$W" 1
sleep 2
stop_at_menu $P
[ -f "$RDIR/recent.nrp" ] || fail "S3: cheat run should still rotate into recent"
[ "$(field "$RDIR/recent.nrp" cheated)" = 1 ] || fail "S3: recent not cheat-flagged"
[ -f "$RDIR/best.nrp" ] && fail "S3: cheat run must never become best"

echo "===== S4: game over -> ENDED header, rotation, save deleted ====="
use_home s4
# Start deep in the game (dev cheat env): seekers home on the player and ram
# lethally, so three spawns die fast. The cheat flag doesn't matter here —
# S4 asserts the ENDED/rotation/save-delete mechanics, not best promotion.
P=$(NEWTONIA_BETA=1 NEWTONIA_START_GENERATION=12 "$ROOT/newtonia" \
     > "$OUT/s4.log" 2>&1 & echo $!); sleep 2; W=$(win)
menu_new_game "$W" 0
GAMEOVER=0
for i in $(seq 1 60); do
  key "$W" space   # spawn each fresh life straight into the seekers
  sleep 1.5
  if grep -q "replay: run ended" "$OUT/s4.log"; then GAMEOVER=1; break; fi
  kill -0 $P 2>/dev/null || { fail "S4: game crashed"; break; }
done
[ "$GAMEOVER" = 1 ] || fail "S4: never reached game over"
sleep 1
stop_at_menu $P
[ -f "$RDIR/current.nrp" ] && fail "S4: current.nrp should be gone after game over"
[ -f "$RDIR/recent.nrp" ] || fail "S4: game over did not rotate into recent"
[ "$(field "$RDIR/recent.nrp" ended)" = 1 ] || fail "S4: header not ENDED"
[ "$(field "$RDIR/recent.nrp" clean)" = 1 ] || fail "S4: header not CLEAN"
[ "$(field "$RDIR/recent.nrp" duration_ms)" -gt 0 ] || fail "S4: zero duration"
LOGSCORE=$(sed -n 's/.*replay: run ended (score=\([0-9]*\).*/\1/p' "$OUT/s4.log" | head -1)
[ "$(field "$RDIR/recent.nrp" score)" = "$LOGSCORE" ] || fail "S4: header score != logged score"
[ -f "$SAVE" ] && fail "S4: savegame should be deleted at game over"

echo "===== S5: crash artifact -> recent with stale header, no best ====="
use_home s5
P=$(launch_game s5); sleep 2; W=$(win)
menu_new_game "$W" 0
sleep 3
key "$W" p; sleep 1                            # pause: saves + flushes records
key "$W" p; sleep 2                            # unpause, play on (RAM only)
kill -9 $P; sleep 1                            # hard crash: header never patched
[ -f "$RDIR/current.nrp" ] || fail "S5: no current.nrp after crash"
[ "$(field "$RDIR/current.nrp" clean)" = 0 ] || fail "S5: crashed header should be stale"
[ "$(field "$RDIR/current.nrp" deltas)" -gt 0 ] || fail "S5: pause flush left no records"
P=$(launch_game s5b); sleep 2; W=$(win)
menu_new_game "$W" 1                           # discard the crashed run
sleep 2
stop_at_menu $P
[ -f "$RDIR/recent.nrp" ] || fail "S5: crashed run should rotate into recent"
[ "$(field "$RDIR/recent.nrp" clean)" = 0 ] || fail "S5: recent should keep the stale header"
[ -f "$RDIR/best.nrp" ] && fail "S5: stale-header run must get no best check"

echo
if [ "$FAIL" = 0 ]; then echo "REPLAY-R1-OK"; else echo "REPLAY-R1-FAIL"; exit 1; fi
