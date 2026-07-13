#!/bin/bash
# Rejoin-flap regression (the "wifi off 40 s, CONNECTION LOST on re-enable"
# bug): connect via room code, sever the host transport so the joiner's
# in-game watchdog trips and it AUTO-rejoins, then force the freshly
# re-established rejoin transport to flap once before the first snapshot
# (NEWTONIA_NET_TEST_REJOIN_FLAP=1). The lobby used to treat that post-
# handshake flap as terminal ("CONNECTION LOST" / LobbyFailed); it must now
# retry within the rejoin budget and reconnect. Prints REJOINFLAP-E2E-OK on
# success. See TESTING.md.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

join_with_code() {  # WINDOW CODE: menu -> ONLINE -> JOIN -> type code
  local W=$1 CODE=$2 c
  key $W Return; sleep 1; key $W s; key $W Return; sleep 1
  key $W s; key $W Return; sleep 1
  for c in $(echo "$CODE" | grep -o .); do key $W "$c"; done
}

# Host: sever its transport 14 s into the game so the joiner auto-rejoins.
export NEWTONIA_NET_TEST_DROP_TRANSPORT_MS=14000
PA=$(launch host)
unset NEWTONIA_NET_TEST_DROP_TRANSPORT_MS
sleep 2
# Joiner: flap the first re-established rejoin transport exactly once.
export NEWTONIA_NET_TEST_REJOIN_FLAP=1
PB=$(launch joiner)
unset NEWTONIA_NET_TEST_REJOIN_FLAP
sleep 4

WINS=$(newtonia_windows)
A=$(echo "$WINS" | head -1); B=$(echo "$WINS" | tail -1)
[ "$A" != "$B" ] || { echo "only one window"; exit 1; }

key $A Return; sleep 1; key $A s; key $A Return; sleep 1; key $A Return
CODE=$(host_room_code host)
[ -n "$CODE" ] || { echo "NO ROOM CODE"; kill $PA $PB; exit 1; }
echo "room code: $CODE"

join_with_code $B "$CODE"
echo "== waiting for connect + bootstrap"; sleep 16
alive $PA host; alive $PB joiner
grep -aq "bootstrap adopted" "$OUT/joiner.log" || { echo "NO INITIAL BOOTSTRAP"; exit 1; }

# The host's 14 s timer drops its transport; the joiner watchdog (10 s) then
# auto-rejoins, hits the injected flap, retries, and re-bootstraps. Give the
# whole loop room to complete (watchdog + rejoin + flap-retry + rejoin).
echo "== waiting for drop -> auto-rejoin -> flap -> retry -> re-bootstrap"
for i in $(seq 1 40); do
  sleep 1
  alive $PA host; alive $PB joiner
  [ "$(grep -ac 'bootstrap adopted' "$OUT/joiner.log")" -ge 2 ] && break
done

kill $PA $PB 2>/dev/null; wait $PA $PB 2>/dev/null
assert_clean "$OUT/host.log" "$OUT/joiner.log"

# The joiner must have: entered auto-rejoin, hit the injected flap, retried
# on it (the fix), and NOT given up the budget.
grep -aq "auto-rejoining room" "$OUT/joiner.log" || { echo "NO AUTO-REJOIN"; exit 1; }
grep -aq "TEST injecting rejoin transport flap" "$OUT/joiner.log" || {
  echo "FLAP NEVER INJECTED (rejoin never reached Connected)"; exit 1; }
grep -aq "rejoin retry in .*transport" "$OUT/joiner.log" || {
  echo "FLAP NOT RETRIED (fix not engaged)"; exit 1; }
grep -aq "rejoin gave up" "$OUT/joiner.log" && { echo "REJOIN GAVE UP"; exit 1; }
boots=$(grep -ac "bootstrap adopted" "$OUT/joiner.log")
[ "$boots" -ge 2 ] || { echo "NO RE-BOOTSTRAP (boots=$boots)"; exit 1; }
echo "bootstraps: $boots"
echo "REJOINFLAP-E2E-OK"
