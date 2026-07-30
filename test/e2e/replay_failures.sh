#!/bin/bash
# Replay recorder/reader FAILURE paths (REPLAY.md storage model). The other
# replay drivers exercise the happy path; these are the ones that only happen
# when something has already gone wrong, and each is a bug that shipped:
#   S1  a resumed recording whose leftover ends in a TRUNCATED record trims
#       the stub first — appending behind it puts the whole resumed session
#       where no reader ever arrives (the reader stops at the break)
#   S2  a SHORT write (filesystem full) stops the recording, trims back to
#       the last intact boundary and leaves an honest header — instead of
#       recording the rest of the run into that same unreachable void
#   S3  a HOSTILE file is declined, not fatal: a keyframe payload claiming
#       0xFFFFFFF0 weapons used to reach std::vector::resize and terminate
#       the process (69 bytes, and the identical parse runs on a peer's
#       netplay snapshot — see net_state_sane / read_count)
# Prints REPLAY-FAIL-PATHS-OK on success.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
# Recording defaults ON, but be explicit like the other replay drivers.
export NEWTONIA_REPLAY_ENABLE=1

CHECK="$ROOT/test/e2e/replay_check.py"
FAIL=0
fail() { echo "FAIL: $*"; FAIL=1; }
field() { python3 "$CHECK" "$1" | sed -n "s/^$2=//p"; }
win() { xdotool search --name Newtonia 2>/dev/null | tail -1; }

use_home() {
  export XDG_DATA_HOME="$OUT/xdg-$1"
  RDIR="$XDG_DATA_HOME/cc.gfm/newtonia/replays"
}

# kill -9 (see replay.sh): a TERM'd game lingers simulating in the background
# and its focus-loss flush corrupts the next scenario's assertions.
stop_hard() { kill -9 "$1" 2>/dev/null; wait "$1" 2>/dev/null; sleep 0.5; }

# play SECONDS LOGNAME new|continue — run a game, then Esc back to the menu
# (a clean abandon: header patched, current.nrp left resumable).
play() {
  local secs=$1 log=$2 cont=$3
  "$ROOT/newtonia" > "$OUT/$log.log" 2>&1 & local P=$!
  sleep 2; local W=$(win)
  key "$W" Return                 # attract -> menu
  key "$W" Return                 # CONTINUE (first row with a save) / NEW GAME
  sleep "$secs"
  key "$W" Escape; sleep 1.5
  stop_hard $P
}

# play_back PATH LOGNAME — watch a file to the end (60 s cap)
play_back() {
  NEWTONIA_REPLAY_PLAY="$1" "$ROOT/newtonia" > "$OUT/$2.log" 2>&1 & local P=$!
  local i
  for i in $(seq 1 60); do
    grep -q "playback finished" "$OUT/$2.log" && break
    sleep 1
  done
  stop_hard $P
}

echo "===== S1: resume over a truncated tail ====="
use_home s1
play 6 rec1 new
[ -f "$RDIR/current.nrp" ] || fail "S1: nothing recorded"
BEFORE=$(field "$RDIR/current.nrp" records)
echo "S1: recorded $BEFORE records"

# The crash artifact: a record frame claiming 4000 payload bytes with 40
# present — exactly what a flush cut mid-append leaves behind.
python3 - "$RDIR/current.nrp" <<'PY'
import struct, sys
with open(sys.argv[1], 'ab') as f:
    f.write(struct.pack('<IBI', 999999, 2, 4000) + b'\x00' * 40)
PY
[ "$(field "$RDIR/current.nrp" truncated_tail)" = 1 ] || fail "S1: fixture is not truncated"

play 6 rec2 continue
grep -q "resuming recording" "$OUT/rec2.log" || fail "S1: did not resume the file"
grep -q "trimming .* unreadable byte" "$OUT/rec2.log" || fail "S1: did not trim the stub"
AFTER=$(field "$RDIR/current.nrp" records)
echo "S1: after the resume: $AFTER records"
[ "${AFTER:-0}" -gt "${BEFORE:-0}" ] ||
  fail "S1: the resumed session is unreachable (record count did not grow)"
[ "$(field "$RDIR/current.nrp" truncated_tail)" = 0 ] || fail "S1: still truncated"
[ "$(field "$RDIR/current.nrp" first_kind)" = keyframes ] || fail "S1: no leading keyframe"
play_back current play1
grep -q "playback started" "$OUT/play1.log" || fail "S1: playback declined the file"
LAST=$(grep -o "playback finished (slot [0-9]*)" "$OUT/play1.log" | grep -o "[0-9]*")
echo "S1: played back to slot ${LAST:-none}"
# Both ~6 s segments, so ~120 slots. Well over one segment is the assertion:
# before the trim, playback stopped at the break and lost everything after.
[ "${LAST:-0}" -gt 70 ] || fail "S1: playback stopped early (slot ${LAST:-0})"

echo "===== S2: a short write (filesystem full) stops the recording ====="
# A real short write, not a failed open: a 64 KB tmpfs fills partway through a
# chunk append, so fwrite writes some of it and then fails. Needs mount, so it
# SKIPS rather than fails where that is not available.
SMALL="$OUT/small"
mkdir -p "$SMALL"
if sudo -n mount -t tmpfs -o size=64k,mode=777 tmpfs "$SMALL" 2>/dev/null; then
  export XDG_DATA_HOME="$SMALL"
  RDIR="$SMALL/cc.gfm/newtonia/replays"
  "$ROOT/newtonia" > "$OUT/rec3.log" 2>&1 & P=$!
  sleep 2; W=$(win)
  key "$W" Return; key "$W" Return               # NEW GAME
  # Plain play: ~2.4 KB of records per second, and every pause/unpause is a
  # checkpoint flush, so the appends keep landing while the space runs out.
  for i in $(seq 1 22); do sleep 2; key "$W" p; key "$W" p; done
  key "$W" Escape; sleep 1.5
  stop_hard $P
  cp -f "$RDIR/current.nrp" "$OUT/full.nrp"
  sudo -n umount "$SMALL"
  grep -q "recording stopped (write of" "$OUT/rec3.log" ||
    fail "S2: the short write went undetected (did the fixture fill up?)"
  [ "$(field "$OUT/full.nrp" truncated_tail)" = 0 ] ||
    fail "S2: left an unreadable truncated tail"
  echo "S2: survivor has $(field "$OUT/full.nrp" records) records, dur=$(field "$OUT/full.nrp" duration_ms)ms"
  play_back "$OUT/full.nrp" play2
  grep -q "playback finished" "$OUT/play2.log" || fail "S2: the survivor does not play back"
else
  echo "S2: SKIPPED (no unprivileged mount available)"
fi

echo "===== S3: a hostile file is declined, not fatal ====="
use_home s3
mkdir -p "$RDIR"
# A structurally valid replay (magic, format version, one keyframe record)
# whose GameState payload claims 0xFFFFFFF0 primary weapons.
python3 - "$RDIR/current.nrp" <<'PY'
import struct, sys
payload  = struct.pack('<i', 1)                    # generation
payload += struct.pack('<ff', 2500.0, 2500.0)      # world
payload += struct.pack('<B', 0)                    # level_cleared
payload += struct.pack('<ii', 0, 0)                # timers
payload += struct.pack('<I', 1)                    # one player
payload += struct.pack('<iiii', 0, 3, 0, 0)        # score/lives/kills/ktl
payload += struct.pack('<ffffff', 0,0, 1,0, 0,0)   # pos/facing/vel
payload += struct.pack('<I', 0xFFFFFFF0)           # <- primary weapon count
hdr = bytearray(64)
struct.pack_into('<IHH', hdr, 0, 0x5052574E, 1, 64)  # NWRP, format 1, size 64
struct.pack_into('<QQ', hdr, 32, 0x1234, 0)          # run id, date
hdr[49] = 1                                          # player count
with open(sys.argv[1], 'wb') as f:
    f.write(bytes(hdr))
    f.write(struct.pack('<IBI', 0, 1, len(payload)) + payload)   # KEYFRAME
PY
# Declining falls back to the MENU, which runs forever — so this waits for the
# decline (or a death) rather than for the process to exit. Surviving IS the
# assertion: before the count bound, the parse hit vector::resize and the
# process was gone inside a second with SIGABRT (134).
NEWTONIA_REPLAY_PLAY=current "$ROOT/newtonia" > "$OUT/hostile.log" 2>&1 & P=$!
for i in $(seq 1 20); do
  grep -q "declining" "$OUT/hostile.log" && break
  kill -0 $P 2>/dev/null || break
  sleep 1
done
if kill -0 $P 2>/dev/null; then
  echo "S3: still running after the hostile file"
  stop_hard $P
else
  wait $P 2>/dev/null; RC=$?
  fail "S3: the hostile file killed the game (exit $RC; 134 = SIGABRT)"
fi
grep -q "unparseable - declining" "$OUT/hostile.log" ||
  fail "S3: the file was not declined (see $OUT/hostile.log)"
! grep -q "playback started" "$OUT/hostile.log" || fail "S3: hostile file started playing"
echo "S3: declined cleanly, process survived"

[ $FAIL = 0 ] && echo "REPLAY-FAIL-PATHS-OK" || echo "REPLAY-FAIL-PATHS-FAIL"
exit $FAIL
