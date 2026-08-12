#!/bin/bash
# REPLAY.md R2 exit criteria, headless:
#   S1  record a short run (shots, a level skip -> generation banner event,
#       an abandon) — the input for everything below
#   S2  play it back at 1x: world unfolds (screenshots differ over time),
#       pause/unpause survives, playback reaches the end, Esc exits
#   S3  speed keys: at x4 the same file finishes in well under half the
#       real-time duration
#   S4  a 2-player recording plays back (split-screen, "2 players" log)
#   S5  a game-over recording plays through to the GAME OVER card (the
#       forced final keyframe carries the ended world into the file)
#   S6  weapon visual effects round-trip: an ALL_WEAPONS run firing the
#       whole arsenal records REC_EFFECT records, and playback shows the
#       lance flash, shock arc and nova ring via the net receive paths
#   S7  a recording that ends with the shield hum on leaves the REPLAY ENDED
#       screen SILENT (the records drive the loop; nothing turns it off once
#       they stop) — checked against the mixer's actual output
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
# Recording defaults OFF (opt-in ship posture); force it on for these drivers.
export NEWTONIA_REPLAY_ENABLE=1

FAIL=0
fail() { echo "FAIL: $*"; FAIL=1; }

CHECK="$ROOT/test/e2e/replay_check.py"
# field FILE NAME -> value from replay_check.py
field() { python3 "$CHECK" "$1" | sed -n "s/^$2=//p"; }

use_home() {
  export XDG_DATA_HOME="$OUT/xdg-$1"
  RDIR="$XDG_DATA_HOME/cc.gfm/newtonia/replays"
}

launch_game() { "$ROOT/newtonia" > "$OUT/$1.log" 2>&1 & echo $!; }
launch_playback() {
  NEWTONIA_REPLAY_PLAY=current "$ROOT/newtonia" > "$OUT/$1.log" 2>&1 & echo $!;
}
win() { xdotool search --name Newtonia | tail -1; }
stop_hard() { kill -9 "$1" 2>/dev/null; wait "$1" 2>/dev/null; sleep 0.5; }

# The current game's pid rides $P throughout; kill it on ANY exit so a
# failed assertion never leaks a live game under Xvfb (they linger as
# zombies whose focus-loss flushes corrupt later runs' assertions).
P=""
trap 'kill -9 $P 2>/dev/null' EXIT

# wait_log LOG PATTERN TIMEOUT_S -> 0 when the pattern appears
wait_log() {
  local i
  for i in $(seq 1 $(($3 * 2))); do
    grep -q "$2" "$OUT/$1.log" && return 0
    sleep 0.5
  done
  return 1
}

record_run() {  # record_run NAME ADD_P2
  P=$(launch_game "$1"); sleep 2; W=$(win)
  key "$W" Return; sleep 0.5; key "$W" Return   # attract -> NEW GAME
  sleep 0.5; key "$W" space                     # spawn
  [ "$2" = 1 ] && { key "$W" Return; sleep 0.5; }  # add player 2
  xdotool keydown --window "$W" d
  for i in $(seq 1 25); do xdotool key --window "$W" space; sleep 0.1; done
  xdotool keyup --window "$W" d
  key "$W" n; sleep 1                           # level skip -> intro
  key "$W" space; sleep 3                       # dismiss intro, play on
  key "$W" Escape; sleep 2                      # abandon (CLEAN header)
  stop_hard $P
  [ -f "$RDIR/current.nrp" ] || fail "$1: no current.nrp recorded"
}

echo "===== S1: record the input run ====="
use_home p1
record_run rec1 0
DUR_MS=$(python3 "$ROOT/test/e2e/replay_check.py" "$RDIR/current.nrp" | sed -n 's/^duration_ms=//p')
echo "recorded ${DUR_MS} ms"
[ "${DUR_MS:-0}" -gt 4000 ] || fail "S1: recording too short (${DUR_MS} ms)"

echo "===== S2: playback at 1x — world unfolds, pause survives, Esc exits ====="
P=$(launch_playback play1); sleep 2; W=$(win)
wait_log play1 "replay: playback started" 10 || fail "S2: playback never started"
sleep 1;  shot "$W" play-t1
sleep 2;  shot "$W" play-t2
sleep 2;  shot "$W" play-t3
cmp -s "$OUT/play-t1.png" "$OUT/play-t2.png" && fail "S2: t1==t2 (world frozen?)"
cmp -s "$OUT/play-t2.png" "$OUT/play-t3.png" && fail "S2: t2==t3 (world frozen?)"
# Pause must FREEZE the recorded world, not just draw a card over it. Both
# things that advance playback — the replay clock in tick_replay_poll and the
# ghost extrapolation — live inside tick_net_client, which for a long time
# never consulted `running`: the pause text sat over a world still playing on
# underneath (field: Android, 2026-07-27). Screenshotting either side of a
# wait is the only way to see that; "playback finished" arriving still only
# proves the unpause worked.
key "$W" p
# Wait for the pause to TAKE EFFECT before timing it: the old fixed 1 s
# settle was enough locally and not on a CI runner, where the first shot
# still caught a moving world and the comparison failed as "world still
# moving while paused" — the product invariant reported broken by a
# screenshot taken too early (2026-08-12). Frozen is observable: two frames
# in a row that match. If they never do, playback really is running on
# underneath, and this loop fails for the right reason.
# "Frozen" is measured in pixels changed, not in bytes equal. A byte compare
# passed locally and failed on a CI runner with no way to tell which of two
# very different things had happened — a world still playing, or a software
# rasteriser painting an edge differently between frames. FREEZE_TOL is far
# below anything real motion produces (a drifting asteroid field redraws tens
# of thousands of pixels) and above rasteriser noise; the counts print either
# way, so the next failure says which it was.
FREEZE_TOL="${FREEZE_TOL:-800}"
frozen=
shot "$W" pause-settle-0
for i in 1 2 3 4 5 6 7 8 9 10; do
  sleep 0.5
  shot "$W" "pause-settle-$i"
  D=$(frame_delta "$OUT/pause-settle-$((i - 1)).png" "$OUT/pause-settle-$i.png")
  if [ "$D" -le "$FREEZE_TOL" ]; then
    frozen=1; cp "$OUT/pause-settle-$i.png" "$OUT/play-pause1.png"
    echo "S2: froze after $((i / 2))s (delta ${D}px)"
    break
  fi
done
[ -n "$frozen" ] || fail "S2: world never froze after the pause key (last delta ${D}px)"
# ...and having frozen, it must STAY frozen: this is the assertion that
# caught the field bug where the replay clock ignored `running`.
sleep 3
shot "$W" play-pause2
D=$(frame_delta "$OUT/play-pause1.png" "$OUT/play-pause2.png")
echo "S2: paused 3s delta ${D}px"
[ "$D" -le "$FREEZE_TOL" ] || fail "S2: world still moving while paused (${D}px changed)"
key "$W" p; sleep 2
shot "$W" play-resumed
# The mirror image of the freeze check, and it needs the same tolerance: on a
# rig where a still frame can differ by a few pixels, "not byte-identical" is
# not evidence that playback resumed.
D=$(frame_delta "$OUT/play-pause2.png" "$OUT/play-resumed.png")
echo "S2: unpause delta ${D}px"
[ "$D" -gt "$FREEZE_TOL" ] || fail "S2: unpause did not resume playback (${D}px changed)"
wait_log play1 "replay: playback finished" $(( DUR_MS / 1000 + 15 )) \
  || fail "S2: playback never finished"
alive $P play1
key "$W" Escape; sleep 2                        # exit to menu
alive $P play1
shot "$W" play-menu
stop_hard $P
grep -q "replay: playback started" "$OUT/play1.log" || fail "S2: lost the log?"

echo "===== S3: x4 speed finishes fast ====="
P=$(launch_playback play4); sleep 2; W=$(win)
wait_log play4 "replay: playback started" 10 || fail "S3: playback never started"
key "$W" equal; key "$W" equal                  # x2 -> x4
T0=$(date +%s)
wait_log play4 "replay: playback finished" $(( DUR_MS / 1000 + 15 )) \
  || fail "S3: playback never finished at x4"
T1=$(date +%s)
ELAPSED=$((T1 - T0))
echo "x4 finished in ${ELAPSED}s (recording is $(( DUR_MS / 1000 ))s)"
[ "$ELAPSED" -le $(( DUR_MS / 2000 + 3 )) ] \
  || fail "S3: x4 too slow (${ELAPSED}s for a $(( DUR_MS / 1000 ))s file)"
stop_hard $P

echo "===== S4: 2-player recording plays back split-screen ====="
use_home p2
record_run rec2 1
grep -q "" /dev/null  # keep set -u happy on empty branches
P=$(launch_playback play2p); sleep 2; W=$(win)
wait_log play2p "replay: playback started" 10 || fail "S4: playback never started"
grep -q "2 players" "$OUT/play2p.log" || fail "S4: not a 2-player playback"
wait_log play2p "replay: player 2 joined" 10 \
  || fail "S4: ghost roster never grew (split-screen never engaged)"
sleep 2; shot "$W" play-2p                      # split-screen for the humans
sleep 1; shot "$W" play-2p-b
cmp -s "$OUT/play-2p.png" "$OUT/play-2p-b.png" && fail "S4: 2P world frozen"
stop_hard $P

echo "===== S5: game-over recording reaches the GAME OVER card ====="
use_home p3
P=$(NEWTONIA_BETA=1 NEWTONIA_START_GENERATION=12 "$ROOT/newtonia" \
     > "$OUT/rec3.log" 2>&1 & echo $!); sleep 2; W=$(win)
key "$W" Return; sleep 0.5; key "$W" Return   # attract -> NEW GAME
GAMEOVER=0
for i in $(seq 1 60); do
  key "$W" space   # spawn each fresh life straight into the seekers
  sleep 1.5
  if grep -q "replay: run ended" "$OUT/rec3.log"; then GAMEOVER=1; break; fi
  kill -0 $P 2>/dev/null || { fail "S5: recording game crashed"; break; }
done
[ "$GAMEOVER" = 1 ] || fail "S5: never reached game over while recording"
sleep 1; stop_hard $P
[ -f "$RDIR/recent.nrp" ] || fail "S5: game-over run did not rotate to recent"

P=$(NEWTONIA_REPLAY_PLAY=recent "$ROOT/newtonia" > "$OUT/play-go.log" 2>&1 & echo $!)
sleep 2; W=$(win)
wait_log play-go "replay: playback started" 10 || fail "S5: playback never started"
wait_log play-go "playback finished" 90 || fail "S5: playback never finished"
# The recorded final death must land: the all-out latch logs the card.
wait_log play-go "game over (all players out)" 10 \
  || fail "S5: GAME OVER card never latched (final keyframe missing?)"
sleep 1; shot "$W" play-gameover
stop_hard $P

echo "===== S6: weapon effect visuals round-trip ====="
use_home p4
P=$(NEWTONIA_ALL_WEAPONS=1 "$ROOT/newtonia" > "$OUT/rec4.log" 2>&1 & echo $!)
sleep 2; W=$(win)
key "$W" Return; sleep 0.5; key "$W" Return   # attract -> NEW GAME
sleep 0.5; key "$W" space                     # request spawn
sleep 3.5                                     # ...and wait out the countdown:
                                              # a dead ship ignores fire, so
                                              # cycling early records nothing
# The grant's primary list is [weapon_configs, beam, lance, shock] with shock
# selected — 25 entries today, and weapon_configs (ship.cpp) is the part that
# grows: the burst rows took it from 15 to 22 and this loop, still walking 20
# positions, silently stopped reaching beam and lance. Shock kept passing
# only because the grant leaves it selected. So walk MORE than the whole list
# and keep it that way when weapons are added — with slack on top, because a
# death re-grants the arsenal (NEWTONIA_ALL_WEAPONS re-arms on respawn) and
# puts the selection back on shock, rewinding the walk. Not TOO much slack:
# the recording is played back at 1x below, so every extra position costs
# twice. Fire TWICE at every position (then advance), because single pulls
# flaked when Xvfb dropped keypresses. Then every secondary (x fires the
# selection; nova mints its ring).
for i in $(seq 1 40); do key "$W" space; key "$W" space; key "$W" q; done
sleep 1
for i in $(seq 1 5); do key "$W" c; key "$W" x; sleep 0.3; done
sleep 2
key "$W" Escape; sleep 2
stop_hard $P
FX=$(field "$RDIR/current.nrp" effects)
echo "recorded $FX effect records"
[ "${FX:-0}" -ge 3 ] || fail "S6: expected lance+shock+nova effects, got ${FX:-0}"

P=$(NEWTONIA_REPLAY_PLAY=current "$ROOT/newtonia" > "$OUT/play-fx.log" 2>&1 & echo $!)
sleep 2; W=$(win)
wait_log play-fx "replay: playback started" 10 || fail "S6: playback never started"
wait_log play-fx "playback finished" 120 || fail "S6: playback never finished"
grep -q "lance pulse received" "$OUT/play-fx.log" || fail "S6: lance flash never played back"
grep -q "shock bolt received" "$OUT/play-fx.log"  || fail "S6: shock arc never played back"
grep -q "replay ring" "$OUT/play-fx.log"          || fail "S6: nova/giga ring never played back"
stop_hard $P

echo "===== S7: the ended-replay screen is silent ====="
# Continuous loops in playback are driven by the records (net_apply_state sets
# the shield hum from each snapshot's invincibility). Past the last record the
# world freezes and no snapshot ever arrives to turn one off, so a recording
# that ENDS with the hum on used to drone forever behind the REPLAY ENDED card.
# Land inside a hum window on purpose: the spawn countdown is 4000 ms and the
# spawn invincibility that follows is 1500 ms, so quit at ~4.8 s and the last
# records carry a live, invincible ship.
use_home p5
P=$(launch_game rec5); sleep 2; W=$(win)
key "$W" Return; sleep 0.5; key "$W" Return   # attract -> NEW GAME
sleep 4.8
kill $P; wait $P 2>/dev/null                  # SIGTERM: clean quit, tail flushed
sleep 0.5
# Play it back with SDL writing the mixer's output to a file. Everything after
# the "playback finished" mark must be digital silence — and the part before it
# must NOT be, or a mixer that never opened would pass this vacuously.
RAW="$OUT/hum.raw"
P=$(SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE="$RAW" NEWTONIA_REPLAY_PLAY=current \
      "$ROOT/newtonia" > "$OUT/play-hum.log" 2>&1 & echo $!)
sleep 2
if wait_log play-hum "playback finished" 60; then
  MARK=$(stat -c %s "$RAW" 2>/dev/null || echo 0)
  sleep 4                                     # ...of REPLAY ENDED screen
  stop_hard $P
  python3 - "$RAW" "$MARK" <<'PY' || fail "S7: hum still playing on the ended-replay screen"
import array, sys
data = open(sys.argv[1], 'rb').read()
mark = int(sys.argv[2])
def peak(b):
    a = array.array('h'); a.frombytes(b[:len(b) // 2 * 2])
    return max((abs(v) for v in a), default=0)
# Skip a buffer's worth past the mark so audio already queued when the file
# ended isn't counted against the frozen screen.
head, tail = peak(data[:mark]), peak(data[mark + 65536:])
print("S7: peak before=%d after=%d" % (head, tail))
if head < 1000:
    print("S7: no audio was written at all - disk driver unusable?")
    sys.exit(1)
sys.exit(1 if tail >= 64 else 0)
PY
else
  fail "S7: playback never finished"
  stop_hard $P
fi

echo
if [ "$FAIL" = 0 ]; then echo "REPLAY-R2-OK"; else echo "REPLAY-R2-FAIL"; exit 1; fi
