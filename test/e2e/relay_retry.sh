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
# test room makes a single offer and cannot. Prints RELAY-RETRY-E2E-OK.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check
export NEWTONIA_NET_TEST_SEATS=4

PA=$(launch host)
sleep 2
PB=$(launch joiner NEWTONIA_NET_TEST_RELAY_FAIL_ONCE=1)
sleep 4

WINS=$(newtonia_windows)
A=$(echo "$WINS" | head -1); B=$(echo "$WINS" | tail -1)
[ "$A" != "$B" ] || { echo "only one window"; kill $PA $PB; exit 1; }

key $A Return; sleep 1; key $A s; key $A Return; sleep 1; key $A Return
CODE=$(host_room_code host)
[ -n "$CODE" ] || { echo "NO ROOM CODE"; kill $PA $PB; exit 1; }
echo "room code: $CODE"

nav_join $B "$CODE" joiner
T0=$(date +%s)
RETRIED=""
for i in $(seq 1 40); do
  sleep 1; alive $PA host; alive $PB joiner
  grep -aq "joining room $CODE again with a fresh transport" "$OUT/joiner.log" && {
    RETRIED=$(( $(date +%s) - T0 )); break; }
done
[ -n "$RETRIED" ] || { echo "NO RETRY"; tail -30 "$OUT/joiner.log"; kill $PA $PB; exit 1; }
echo "== retried after ${RETRIED}s"
grep -aq "relay-only - no TURN relay after" "$OUT/joiner.log" || {
  echo "NO RELAY-FAILURE LINE"; kill $PA $PB; exit 1; }
# 10 s relay wait plus the time the offer took; the old path took ~63 s.
[ "$RETRIED" -le 25 ] || { echo "RETRY TOO SLOW (${RETRIED}s)"; kill $PA $PB; exit 1; }

for i in $(seq 1 30); do
  grep -aq "seat 2 filled" "$OUT/host.log" && break; sleep 1
done
grep -aq "seat 2 filled" "$OUT/host.log" || {
  echo "RETRY NEVER SEATED"; tail -30 "$OUT/joiner.log"; kill $PA $PB; exit 1; }
echo "== seated after the retry; host starts"
key $A Return; sleep 2
for i in $(seq 1 30); do
  grep -aq "bootstrap adopted" "$OUT/joiner.log" && break; sleep 1
done
alive $PA host; alive $PB joiner
grep -aq "bootstrap adopted" "$OUT/joiner.log" || {
  echo "RETRY DID NOT CONNECT"; tail -30 "$OUT/joiner.log"; kill $PA $PB; exit 1; }
[ "$(grep -ac 'relay-only ICE policy ACTIVE' "$OUT/joiner.log")" -eq 1 ] || {
  echo "THE RETRY WAS RELAY-ONLY TOO"; kill $PA $PB; exit 1; }
grep -aq "rejoin\|LobbyFailed\|COULD NOT CONNECT" "$OUT/joiner.log" && {
  echo "THE FIRST FAILURE REACHED THE PLAYER"; kill $PA $PB; exit 1; }
shot $B relay-retry-joiner

kill $PA $PB 2>/dev/null; wait $PA $PB 2>/dev/null
assert_clean "$OUT/host.log" "$OUT/joiner.log"
echo "RELAY-RETRY-E2E-OK"
