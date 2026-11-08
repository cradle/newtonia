#!/bin/bash
# A join whose relay never arrives retries once by itself, at once.
#
# Field, Xbox dev kit 2026-11-02: a relay-only join's TURN Allocates went
# unanswered, libjuice gave up after 23.5 s, ICE failed at 63 s and the
# player read "COULD NOT CONNECT"; joining again by hand worked straight
# away. The transport now reports a relay-only pairing with no relay after
# 10 s (RtcTransport::relay_unavailable) and the lobby joins again once
# with a fresh transport and signalling socket (retry_join_after_relay_failure).
#
# The local relay mints no TURN credentials, so a relay-only transport here
# can never get a relay. NEWTONIA_NET_TEST_RELAY_FAIL_ONCE=1 makes only the
# joiner's FIRST attempt relay-only; the retry joins normally and must
# connect. A 4-seat room, so the host runs the waiting room (what every
# shipped build does) and offers the retry a fresh pairing; the 2-seat
# test room makes a single offer and cannot.
#
# MODE 3 is the other end (kit host, 2026-11-08): the HOST's transport for
# the joiner's first attempt gets no relay, so the host drops that pairing
# and the joiner, its answer sent, sat on FOUND THE HOST / CONNECTING until
# ICE gave up at ~40 s and said COULD NOT CONNECT. The joiner now joins
# again once when its first room join has not connected in 15 s
# (retry_join_after_connect_failure). Prints RELAY-RETRY-E2E-OK.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check
export NEWTONIA_NET_TEST_SEATS=4

# MODE 1: no relay ever arrives, caught by the 10 s wait (after the answer).
# MODE 2: gathering ends at once with no relay and the answer is held back,
# so the failure is found before a session exists (the pre-session path).
run_mode() {
  MODE=$1; LIMIT=$2
  echo "== mode $MODE"
  # Fresh prefs per mode: the first run's save would add a CONTINUE row
  # and shift the menu the key presses below expect.
  export XDG_DATA_HOME="$OUT/xdg$MODE"
  if [ "$MODE" = 3 ]; then
    PA=$(launch host$MODE NEWTONIA_NET_TEST_HOST_RELAY_FAIL_ONCE=1)
    sleep 2
    PB=$(launch joiner$MODE)
  else
    PA=$(launch host$MODE)
    sleep 2
    PB=$(launch joiner$MODE NEWTONIA_NET_TEST_RELAY_FAIL_ONCE=$MODE)
  fi
  sleep 4

  WINS=$(newtonia_windows)
  A=$(echo "$WINS" | head -1); B=$(echo "$WINS" | tail -1)
  [ "$A" != "$B" ] || { echo "only one window"; kill $PA $PB; exit 1; }

  key $A Return; sleep 1; key $A s; key $A Return; sleep 1; key $A Return
  CODE=$(host_room_code host$MODE)
  [ -n "$CODE" ] || { echo "NO ROOM CODE"; kill $PA $PB; exit 1; }
  echo "room code: $CODE"

  nav_join $B "$CODE" joiner$MODE
  T0=$(date +%s)
  RETRIED=""
  for i in $(seq 1 40); do
    sleep 1; alive $PA host; alive $PB joiner
    grep -aq "joining room $CODE again with a fresh transport" "$OUT/joiner$MODE.log" && {
      RETRIED=$(( $(date +%s) - T0 )); break; }
  done
  [ -n "$RETRIED" ] || { echo "NO RETRY"; tail -30 "$OUT/joiner$MODE.log"; kill $PA $PB; exit 1; }
  echo "== retried after ${RETRIED}s"
  RELAY_LOG="$OUT/joiner$MODE.log"
  [ "$MODE" = 3 ] && RELAY_LOG="$OUT/host$MODE.log"
  grep -aq "relay-only - no TURN relay after" "$RELAY_LOG" || {
    echo "NO RELAY-FAILURE LINE"; kill $PA $PB; exit 1; }
  # 10 s relay wait plus the time the offer took; the old path took ~63 s.
  [ "$RETRIED" -le $LIMIT ] || { echo "RETRY TOO SLOW (${RETRIED}s)"; kill $PA $PB; exit 1; }

  for i in $(seq 1 30); do
    grep -aq "seat 2 filled" "$OUT/host$MODE.log" && break; sleep 1
  done
  grep -aq "seat 2 filled" "$OUT/host$MODE.log" || {
    echo "RETRY NEVER SEATED"; tail -30 "$OUT/joiner$MODE.log"; kill $PA $PB; exit 1; }
  echo "== seated after the retry; host starts"
  key $A Return; sleep 2
  for i in $(seq 1 30); do
    grep -aq "bootstrap adopted" "$OUT/joiner$MODE.log" && break; sleep 1
  done
  alive $PA host; alive $PB joiner
  grep -aq "bootstrap adopted" "$OUT/joiner$MODE.log" || {
    echo "RETRY DID NOT CONNECT"; tail -30 "$OUT/joiner$MODE.log"; kill $PA $PB; exit 1; }
  if [ "$MODE" = 2 ]; then
    grep -aq "transport failed during signaling" "$OUT/joiner$MODE.log" || {
      echo "MODE 2 DID NOT TAKE THE PRE-SESSION PATH"; kill $PA $PB; exit 1; }
  fi
  if [ "$MODE" = 3 ]; then
    grep -aq "first join not connected in time" "$OUT/joiner$MODE.log" || {
      echo "MODE 3 DID NOT TAKE THE CONNECT-TIMEOUT PATH"; kill $PA $PB; exit 1; }
    [ "$(grep -ac 'relay-only ICE policy ACTIVE' "$OUT/host$MODE.log")" -eq 1 ] || {
      echo "THE HOST'S SECOND TRANSPORT WAS RELAY-ONLY TOO"; kill $PA $PB; exit 1; }
  else
    [ "$(grep -ac 'relay-only ICE policy ACTIVE' "$OUT/joiner$MODE.log")" -eq 1 ] || {
      echo "THE RETRY WAS RELAY-ONLY TOO"; kill $PA $PB; exit 1; }
  fi
  grep -aq "rejoin\|LobbyFailed\|COULD NOT CONNECT" "$OUT/joiner$MODE.log" && {
    echo "THE FIRST FAILURE REACHED THE PLAYER"; kill $PA $PB; exit 1; }
  shot $B relay-retry-joiner$MODE

  kill $PA $PB 2>/dev/null; wait $PA $PB 2>/dev/null
  assert_clean "$OUT/host$MODE.log" "$OUT/joiner$MODE.log"
}

# MODE 4: a cold invite (+connect CODE, as the platform invite backends
# hand it over) whose first attempt is relay-only and so FAILS with no
# relay at ~10 s, before the 15 s connect timer. The invite rides the
# rejoin ctor but not its retry loop, so the auto-rejoin helper must not
# claim it (that retry never fired and stranded it on JOINING THE ROOM);
# its one retry is the first-join one (PR #641 review).
run_invite() {
  echo "== mode 4 (cold invite)"
  export XDG_DATA_HOME="$OUT/xdg4"
  PA=$(launch host4)
  sleep 4
  A=$(newtonia_windows | head -1)
  key $A Return; sleep 1; key $A s; key $A Return; sleep 1; key $A Return
  CODE=$(host_room_code host4)
  [ -n "$CODE" ] || { echo "NO ROOM CODE"; kill $PA; exit 1; }
  echo "room code: $CODE"
  env NEWTONIA_NET_TEST_RELAY_FAIL_ONCE=1 "$ROOT/newtonia" +connect "$CODE" \
    > "$OUT/joiner4.log" 2>&1 & PB=$!
  T0=$(date +%s)
  RETRIED=""
  for i in $(seq 1 30); do
    sleep 1; alive $PA host; alive $PB joiner
    grep -aq "first join failed after .* joining room $CODE again" "$OUT/joiner4.log" && {
      RETRIED=$(( $(date +%s) - T0 )); break; }
  done
  [ -n "$RETRIED" ] || { echo "NO RETRY"; tail -30 "$OUT/joiner4.log"; kill $PA $PB; exit 1; }
  echo "== retried after ${RETRIED}s"
  grep -aq "relay-only - no TURN relay after" "$OUT/joiner4.log" || {
    echo "NO RELAY-FAILURE LINE"; kill $PA $PB; exit 1; }
  # Failed before the 15 s timer: the transport's own failure got it.
  [ "$RETRIED" -le 15 ] || { echo "RETRY TOO SLOW (${RETRIED}s)"; kill $PA $PB; exit 1; }
  grep -aq "rejoin retry\|retrying in" "$OUT/joiner4.log" && {
    echo "THE AUTO-REJOIN LOOP CLAIMED THE INVITE"; kill $PA $PB; exit 1; }
  for i in $(seq 1 30); do
    grep -aq "seat 2 filled" "$OUT/host4.log" && break; sleep 1
  done
  grep -aq "seat 2 filled" "$OUT/host4.log" || {
    echo "INVITE RETRY NEVER SEATED"; tail -30 "$OUT/joiner4.log"; kill $PA $PB; exit 1; }
  grep -aq "cold invite failed" "$OUT/joiner4.log" && {
    echo "THE INVITE FAILED"; kill $PA $PB; exit 1; }
  echo "== invite seated after the retry"
  kill $PA $PB 2>/dev/null; wait $PA $PB 2>/dev/null
  assert_clean "$OUT/host4.log" "$OUT/joiner4.log"
}

run_mode 1 25
sleep 2
run_mode 2 12
sleep 2
run_mode 3 22
sleep 2
run_invite
echo "RELAY-RETRY-E2E-OK"
