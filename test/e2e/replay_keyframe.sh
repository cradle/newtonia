#!/bin/bash
# REPLAY.md keyframe-ordering invariant, headless (no relay, no display, no
# game — the recorder is driven directly by the in-binary selftest).
#
# Why a selftest and not a driven game: the pre-keyframe window this covers is
# structurally unreachable from the game's own call order. replay_start forces
# a keyframe as the first record of every recording, and the host forces one
# for a (re)joining client (glgame.cpp "rejoined client starts from a
# keyframe"), so no amount of xdotool driving offers a record before the
# opening keyframe — which is exactly why the hold path went untested while
# two field bugs (an empty host recording, then a client file that slid its
# world once a second) came out of it. See replay_selftest.cpp.
#
# Asserts both the verdict and the two log lines a field diagnosis depends on,
# with their per-seam counts — a cumulative count reported drops the previous
# seam had already accounted for ("5 held" where 2 were).
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${NEWTONIA_TEST_OUT:-$(mktemp -d /tmp/newtonia-e2e.XXXXXX)}"
mkdir -p "$OUT"
echo "e2e output: $OUT"

[ -x "$ROOT/newtonia" ] || {
  echo "FATAL: $ROOT/newtonia missing - build first: make -j"
  exit 1
}

# Pref-path isolation: the selftest writes a throwaway selftest.nrp in the
# replays dir and deletes it, but never touch a developer's real replays.
# SDL only honours XDG_DATA_HOME on Linux/BSD — on macOS the pref path is
# ~/Library/Application Support regardless, which would put the selftest in
# the developer's real replays dir AND make every litter check below vacuous
# (they'd test a directory that was never created). The dir check after the
# run is what catches that, so this driver stays Linux-only by assertion
# rather than by silently passing.
export XDG_DATA_HOME="$OUT/xdg"
export SDL_AUDIODRIVER=dummy

LOG="$OUT/replay_selftest.log"
NEWTONIA_REPLAY_SELFTEST=1 "$ROOT/newtonia" > "$LOG" 2>&1
RC=$?
FAIL=0
fail() { echo "FAIL: $*"; FAIL=1; }

[ "$RC" = 0 ] || fail "selftest exited $RC"
grep -q "REPLAY SELFTEST PASS" "$LOG" || fail "no PASS verdict"
grep -q "replay: holding records until the opening keyframe" "$LOG" ||
  fail "the hold path never ran - this driver would be vacuous"
grep -q "replay: opening keyframe written (3 record(s) held out)" "$LOG" ||
  fail "first seam should report exactly 3 held"
grep -q "replay: opening keyframe written (2 record(s) held out)" "$LOG" ||
  fail "re-armed seam should report 2 held, not a cumulative 5"
grep -q "replay selftest: records = \[KDDKD\]" "$LOG" ||
  fail "recorded file should be K D D K D"
grep -q "replay selftest: 0 failure(s)" "$LOG" || fail "selftest assertions failed"

# Litter check: the throwaway file must not survive, and the real slots must
# never have been created (the selftest writes beside them, not over them).
R="$XDG_DATA_HOME/cc.gfm/newtonia/replays"
[ -d "$R" ] || fail "replays dir $R never created - pref path not isolated (macOS?), the litter checks below prove nothing"
[ -e "$R/selftest.nrp" ] && fail "selftest.nrp left behind"
for f in current.nrp recent.nrp best.nrp online.nrp; do
  [ -e "$R/$f" ] && fail "selftest touched $f"
done

if [ "$FAIL" = 0 ]; then
  echo "REPLAY-KEYFRAME-OK"
else
  echo "REPLAY-KEYFRAME-FAIL (log: $LOG)"
  exit 1
fi
