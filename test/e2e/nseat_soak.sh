#!/bin/bash
# B6 N-seat generation soak (SEATS=3|4, default 4; SOAK_GENS skips,
# default 15): the room assembles, then the host skips level-by-level —
# through the hazards (9/11/12), the mini-station (10) and the black hole
# (13) — with a liveness check on every instance at every step and a
# short all-hands firing burst every 5th generation. Asserts the snapshot
# telemetry actually advanced, that NO seat dropped at loopback (a loss
# mid-soak is a regression, not weather), and clean logs. Prints
# "NSEAT-SOAK-OK seats=N gen=G". Run on demand like gensoak.sh — not CI.
set -u
SEATS="${SEATS:-4}"
GENS="${SOAK_GENS:-15}"
[ "$SEATS" -ge 3 ] && [ "$SEATS" -le 4 ] || { echo "SEATS must be 3 or 4"; exit 1; }
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

export NEWTONIA_NET_TEST_SEATS=$SEATS
room_setup "$SEATS"

echo "== skipping to generation $GENS"
for g in $(seq 1 "$GENS"); do
  key "${ROOM_WINS[0]}" n
  sleep 3
  room_alive
  if [ $((g % 5)) = 0 ]; then
    for i in 1 2 3; do
      for w in "${ROOM_WINS[@]}"; do
        [ -n "$w" ] && xdotool key --window "$w" space
      done
      sleep 0.3
    done
    shot "${ROOM_WINS[0]}" "nseatsoak$SEATS-host-g$g"
    echo "== gen $g checkpoint alive"
  fi
done

# One more 10 s telemetry slot, then read the generation the snapshots
# saw. Allow a few dropped skips (a countdown can eat a keypress), like
# gensoak's 20-of-25 threshold.
sleep 12
room_alive
GEN=$(grep -a "slot #" "$OUT/host.log" | tail -1 | sed 's/.*gen=\([0-9]*\).*/\1/')
echo "final telemetry generation: ${GEN:-none}"
[ -n "$GEN" ] && [ "$GEN" -ge $((GENS * 4 / 5)) ] ||
  room_fail "soak never reached gen $((GENS * 4 / 5)) (telemetry says '${GEN:-none}')" host

# At loopback no seat may drop: a parked hull or an auto-rejoin inside
# the soak is a real regression.
grep -aq "player [0-9]* lost" "$OUT/host.log" &&
  room_fail "a seat dropped mid-soak" host
for i in $(seq 1 $((SEATS - 1))); do
  grep -aq "auto-rejoining room" "$OUT/joiner$i.log" &&
    room_fail "joiner$i dropped and auto-rejoined mid-soak" "joiner$i"
done

room_kill_all
assert_clean "$OUT"/*.log
echo "NSEAT-SOAK-OK seats=$SEATS gen=$GEN"
