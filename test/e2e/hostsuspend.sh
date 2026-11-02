#!/bin/bash
# Console suspend/resume of the HOST, on the desktop build (NETPLAY_XBOX.md
# item 12, kit68 2026-11-02: the joiner's rejoin after a 30 s host suspend
# took 1.4 s once and 82 s the other time). The host runs the console's
# suspend sequence on SIGUSR1 (NEWTONIA_NET_TEST_SUSPEND, glut.cpp: close
# the links, 150 ms, SIGSTOP); the driver resumes it with SIGCONT after
# SUSPEND_S seconds and times the joiner's auto-rejoin, CYCLES times.
#
# Optional: HOST_BIN (e.g. a NEWTONIA_NET_RELAY_ONLY build, as on the
# console) and TURN="turn:127.0.0.1:3478|user|pass" (a local coturn) to run
# the pairing over TURN like the kit does. Prints HOSTSUSPEND-E2E-OK when
# every cycle rejoined within REJOIN_MAX_S.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

CYCLES=${CYCLES:-2}
SUSPEND_S=${SUSPEND_S:-10}
REJOIN_MAX_S=${REJOIN_MAX_S:-20}
HOST_BIN=${HOST_BIN:-$ROOT/newtonia}
JOINER_BIN=${JOINER_BIN:-$ROOT/newtonia}
TURN_ENV=()
[ -n "${TURN:-}" ] && TURN_ENV=(NEWTONIA_NET_TURN="$TURN")
RTC_ENV=()
[ -n "${RTC_LOG:-}" ] && RTC_ENV=(NEWTONIA_NET_RTC_LOG="$RTC_LOG")

HOST_ENV=()
[ -n "${DROP_CANDS:-}" ] && HOST_ENV=(NEWTONIA_NET_TEST_DROP_REJOIN_CANDS="$DROP_CANDS")
env NEWTONIA_NET_TEST_SUSPEND=1 "${TURN_ENV[@]}" "${RTC_ENV[@]}" "${HOST_ENV[@]}" \
    "$HOST_BIN" > "$OUT/host.log" 2>&1 & PA=$!
sleep 2
env "${TURN_ENV[@]}" "${RTC_ENV[@]}" "$JOINER_BIN" \
    > "$OUT/joiner.log" 2>&1 & PB=$!
sleep 4
fail() { echo "FAIL: $1"; kill -CONT $PA 2>/dev/null; kill -9 $PA $PB 2>/dev/null; exit 1; }

WINS=$(newtonia_windows)
A=$(echo "$WINS" | head -1); B=$(echo "$WINS" | tail -1)
[ "$A" != "$B" ] || fail "only one window"

nav_host $A
CODE=$(host_room_code host)
[ -n "$CODE" ] || fail "no room code"
echo "room code: $CODE"
nav_join $B "$CODE" joiner
echo "== waiting for connect + play"; sleep 18
alive $PA host; alive $PB joiner
grep -aq "bootstrap adopted" "$OUT/joiner.log" || fail "never connected"

worst=0; slow=0
for c in $(seq 1 $CYCLES); do
  before=$(grep -ac "player 2 rejoined" "$OUT/host.log")
  echo "== cycle $c: suspend host for ${SUSPEND_S}s"
  kill -USR1 $PA
  sleep "$SUSPEND_S"
  t0=$(date +%s.%N)
  kill -CONT $PA
  ok=0
  for i in $(seq 1 $((2 * 120))); do
    sleep 0.5
    alive $PA host; alive $PB joiner
    if [ "$(grep -ac "player 2 rejoined" "$OUT/host.log")" -gt "$before" ]; then
      ok=1; break
    fi
  done
  t=$(echo "$(date +%s.%N) - $t0" | bc)
  echo "== cycle $c: rejoined=$ok after ${t}s"
  [ "$ok" = 1 ] || fail "cycle $c never rejoined"
  if [ "$(echo "$t > $REJOIN_MAX_S" | bc)" = 1 ]; then slow=$((slow + 1)); fi
  if [ "$(echo "$t > $worst" | bc)" = 1 ]; then worst=$t; fi
  # The host auto-pauses on the loss; unpause and play a little.
  key $A p; sleep 4
done

kill $PA $PB 2>/dev/null; wait $PA $PB 2>/dev/null
echo "== worst rejoin ${worst}s, $slow slow (> ${REJOIN_MAX_S}s) of $CYCLES"
[ "$slow" = 0 ] || { echo "HOSTSUSPEND-E2E-FAIL"; exit 1; }
echo "HOSTSUSPEND-E2E-OK"
