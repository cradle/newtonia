#!/bin/bash
# Online recording e2e (REPLAY.md "online games record too"): host and
# client each record their session into their own pref dir's
# replays/online.nrp — per-instance XDG_DATA_HOME here, because two
# instances sharing one pref dir would interleave writes into one file (a
# documented dev-loopback quirk, not a supported layout).
#
# S1: connect via room code, both sides fly + shoot, pause/resume (the
#     checkpoint flush) -> both online.nrp files have keyframes + deltas +
#     effects.
# S2: SIGKILL the joiner mid-game (crash artifact, stale header), unpause
#     the host and let it play through the loss (the recorder keeps the
#     cadence while sends are skipped), relaunch the joiner with the SAME
#     pref dir and rejoin -> its log shows "resuming recording" (the
#     run_id seam rode the snapshots) and the file keeps its earlier
#     records (slot numbering continues).
# S3: joiner quits to the menu (clean abandon: header patched, file left
#     for the sweep), host likewise -> both headers clean, host's marked
#     2-player.
# S4: playback smoke on both files (NEWTONIA_REPLAY_PLAY=online): the
#     playback game must come up, tick to the end region, and stay alive.
# S5: the REPLAYS menu lists the session as its own ONLINE RUN row (the
#     4th slot — online.nrp never rotates into recent) and selecting it
#     starts the same playback.
# Prints REPLAY-NET-OK on success. See TESTING.md.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
# Recording defaults OFF (opt-in ship posture); force it on for these drivers.
export NEWTONIA_REPLAY_ENABLE=1
relay_check

XDG_A="$OUT/xdg-host"; XDG_B="$OUT/xdg-joiner"
mkdir -p "$XDG_A" "$XDG_B"
HOST_NRP="$XDG_A/cc.gfm/newtonia/replays/online.nrp"
JOIN_NRP="$XDG_B/cc.gfm/newtonia/replays/online.nrp"
CHECK="$ROOT/test/e2e/replay_check.py"

launch_with() {  # XDG NAME -> pid
  XDG_DATA_HOME="$1" "$ROOT/newtonia" > "$OUT/$2.log" 2>&1 & echo $!
}

field() {  # FILE KEY -> value (0 if absent)
  local v
  v=$(python3 "$CHECK" "$1" 2>/dev/null | grep "^$2=" | cut -d= -f2)
  echo "${v:-0}"
}

PA=""; PB=""; PP=""
trap 'kill -9 $PA $PB $PP 2>/dev/null' EXIT

echo "===== S1: connect, play, checkpoint flush -> both sides recording"
PA=$(launch_with "$XDG_A" host)
sleep 2
PB=$(launch_with "$XDG_B" joiner1)
sleep 4
WINS=$(newtonia_windows)
A=$(echo "$WINS" | head -1); B=$(echo "$WINS" | tail -1)
[ "$A" != "$B" ] || { echo "only one window"; exit 1; }

key $A Return; sleep 1; key $A s; key $A Return; sleep 1; key $A Return
CODE=$(host_room_code host)
[ -n "$CODE" ] || { echo "NO ROOM CODE"; kill $PA $PB; exit 1; }
echo "room code: $CODE"
nav_join $B "$CODE" joiner1
echo "== waiting for connect"; sleep 18
alive $PA host; alive $PB joiner1
grep -aq "bootstrap adopted" "$OUT/joiner1.log" || { echo "NO BOOTSTRAP"; exit 1; }
grep -aq "replay: recording started" "$OUT/host.log"    || { echo "HOST NOT RECORDING"; exit 1; }
grep -aq "replay: recording started" "$OUT/joiner1.log" || { echo "JOINER NOT RECORDING"; exit 1; }

# Fly and shoot on both sides so deltas + FX_SHOT effects exist.
xdotool keydown --window $A w; xdotool keydown --window $B w; sleep 2
xdotool keyup --window $A w; xdotool keyup --window $B w
for i in 1 2 3; do key $A space; key $B space; done
sleep 2
# Pause/resume from the host: EV_PAUSE replicates, and pause is the
# checkpoint flush on BOTH sides — the in-RAM chunks hit the disk.
key $A p; sleep 2; key $A p; sleep 2
alive $PA host; alive $PB joiner1

for F in "$HOST_NRP" "$JOIN_NRP"; do
  [ -f "$F" ] || { echo "MISSING $F"; exit 1; }
  K=$(field "$F" keyframes); D=$(field "$F" deltas); E=$(field "$F" effects)
  echo "$(basename $(dirname $(dirname $F))): keyframes=$K deltas=$D effects=$E"
  [ "$K" -ge 1 ] || { echo "NO KEYFRAMES in $F"; exit 1; }
  [ "$D" -ge 5 ] || { echo "TOO FEW DELTAS in $F"; exit 1; }
  [ "$E" -ge 1 ] || { echo "NO EFFECTS in $F"; exit 1; }
done
JOIN_D1=$(field "$JOIN_NRP" deltas)
HOST_D1=$(field "$HOST_NRP" deltas)

echo "===== S2: SIGKILL joiner, host plays through the loss, rejoin resumes the file"
kill -9 $PB
# A SIGKILLed peer is only detected by the host's 10 s input watchdog (or
# a fast loss detect when the rejoin answer arrives) — WAIT for the
# detection + auto-pause before touching the pause key, or the keypress
# pauses a host that still thinks the game is live.
for i in $(seq 1 20); do
  grep -aq "paused awaiting rejoin" "$OUT/host.log" && break; sleep 1
done
grep -aq "paused awaiting rejoin" "$OUT/host.log" || { echo "NO LOSS DETECT"; exit 1; }
alive $PA host
# Unpause and play on through the loss — the recorder keeps the 10 Hz
# cadence while the sends are skipped.
key $A p
xdotool keydown --window $A w; sleep 3; xdotool keyup --window $A w
alive $PA host

PB=$(launch_with "$XDG_B" joiner2)
sleep 4
for w in $(newtonia_windows); do [ "$w" != "$A" ] && B=$w; done
nav_join $B "$CODE" joiner2
echo "== waiting for rejoin"; sleep 12
alive $PA host; alive $PB joiner2
grep -aq "player 2 rejoined" "$OUT/host.log" || { echo "NO REJOIN"; exit 1; }
grep -aq "replay: resuming recording" "$OUT/joiner2.log" || {
  echo "JOINER DID NOT RESUME THE RECORDING"; exit 1; }

# Play, then a SHORT pause/resume to flush both sides (a >10 s online
# pause trips the host's input watchdog by design — the client stops
# sending INPUT while paused).
xdotool keydown --window $B w; sleep 2; xdotool keyup --window $B w
key $A p; sleep 2; key $A p; sleep 2
alive $PA host; alive $PB joiner2

JOIN_D2=$(field "$JOIN_NRP" deltas)
HOST_D2=$(field "$HOST_NRP" deltas)
echo "joiner deltas $JOIN_D1 -> $JOIN_D2, host deltas $HOST_D1 -> $HOST_D2"
[ "$JOIN_D2" -gt "$JOIN_D1" ] || { echo "JOINER FILE DID NOT GROW ACROSS REJOIN"; exit 1; }
[ "$HOST_D2" -gt "$HOST_D1" ] || { echo "HOST FILE DID NOT GROW"; exit 1; }

echo "===== S3: clean abandons -> patched headers, files left for the sweep"
# Online, Esc is not a quit: it opens the shared pause menu (pausing BOTH
# sides), and the deliberate exit is its RETURN TO MENU row. s clamps past
# the end, so three presses land the last row on the joiner's 2-row menu
# and the host's 3-row (PLAYERS) menu alike.
key $B Escape; sleep 1
key $B s; key $B s; key $B s; key $B Return; sleep 3
kill -0 $PB 2>/dev/null || { echo "joiner died on menu exit"; exit 1; }
# The joiner's Esc already paused the host, and losing its last live peer
# KEEPS that pause (the rejoin latch) — the menu is already up, so no Esc
# here (with the menu open Esc means resume): navigate straight down.
key $A s; key $A s; key $A s; key $A Return; sleep 3
kill -0 $PA 2>/dev/null || { echo "host died on menu exit"; exit 1; }
kill -9 $PA $PB 2>/dev/null; sleep 1

for F in "$HOST_NRP" "$JOIN_NRP"; do
  [ "$(field "$F" clean)" = "1" ] || { echo "HEADER NOT CLEAN: $F"; exit 1; }
  [ "$(field "$F" ended)" = "0" ] || { echo "ABANDON MARKED ENDED: $F"; exit 1; }
done
[ "$(field "$HOST_NRP" players)" = "2" ] || { echo "HOST NOT 2P"; exit 1; }

echo "===== S4: playback smoke on both recordings"
for side in host joiner; do
  XDG=$XDG_A; [ "$side" = joiner ] && XDG=$XDG_B
  XDG_DATA_HOME="$XDG" NEWTONIA_REPLAY_PLAY=online NEWTONIA_NET_DEBUG=1 \
    "$ROOT/newtonia" > "$OUT/play-$side.log" 2>&1 & PP=$!
  sleep 6
  kill -0 $PP 2>/dev/null || { echo "PLAYBACK CRASHED ($side)"; exit 1; }
  W=$(newtonia_windows | tail -1)
  shot $W "replay-online-play-$side"
  sleep 6
  kill -0 $PP 2>/dev/null || { echo "PLAYBACK CRASHED LATE ($side)"; exit 1; }
  kill -9 $PP 2>/dev/null; sleep 1
  grep -aq "replay: cannot play" "$OUT/play-$side.log" && {
    echo "PLAYBACK DECLINED ($side)"; exit 1; }
  grep -aq "replay: playback started" "$OUT/play-$side.log" || {
    echo "PLAYBACK NEVER STARTED ($side)"; exit 1; }
done

echo "===== S5: REPLAYS menu lists the session as ONLINE RUN and plays it"
# Host pref dir holds ONLY online.nrp (online games never save), so the
# menu is NEW GAME / ONLINE / OPTIONS / REPLAYS and the list has a single
# ONLINE RUN row.
PP=$(XDG_DATA_HOME="$XDG_A" "$ROOT/newtonia" > "$OUT/menu-online.log" 2>&1 & echo $!)
sleep 3
W=$(newtonia_windows | tail -1)
key $W Return                                   # dismiss attract
key $W s; key $W s; key $W s; key $W Return     # down to REPLAYS, open
key $W Return                                   # select ONLINE RUN
sleep 3
kill -0 $PP 2>/dev/null || { echo "MENU PLAYBACK CRASHED"; exit 1; }
shot $W replay-online-menu-row
grep -aq "replay: playback started" "$OUT/menu-online.log" || {
  echo "ONLINE RUN ROW DID NOT START PLAYBACK"; exit 1; }
kill -9 $PP 2>/dev/null; sleep 1

assert_clean "$OUT/host.log" "$OUT/joiner1.log" "$OUT/joiner2.log" \
             "$OUT/play-host.log" "$OUT/play-joiner.log" "$OUT/menu-online.log"
echo "REPLAY-NET-OK out=$OUT"
