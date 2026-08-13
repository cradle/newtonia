#!/bin/bash
# Policy-refusal handshake path (net_policy.h / RejectNotAllowed): a host
# whose comms policy refuses every peer (NEWTONIA_NET_TEST_REFUSE_COMMS=1,
# the default backend's inert test hook) must refuse INSIDE the handshake —
# MSG_REJECT before any WELCOME — so the joiner is told honestly and never
# bootstraps, and neither side crashes. Guards the pre-WELCOME chokepoint
# (a post-Ready refusal would ghost the joiner on the CONNECTED screen).
# Prints POLICY-E2E-OK. See TESTING.md.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

PA=$(NEWTONIA_NET_TEST_REFUSE_COMMS=1 launch host)
sleep 2
PB=$(launch joiner)
sleep 4

WINS=$(newtonia_windows)
[ "$(echo "$WINS" | wc -l)" -eq 2 ] ||
  { echo "expected 2 game windows, got: $WINS"; kill_pair $PA $PB; exit 1; }
A=$(echo "$WINS" | head -1); B=$(echo "$WINS" | tail -1)

nav_host $A
CODE=$(host_room_code host)
[ -n "$CODE" ] || { echo "NO ROOM CODE"; kill_pair $PA $PB; exit 1; }
echo "room code: $CODE"

nav_join $B "$CODE" joiner
echo "== waiting for the refusal"; sleep 15
alive $PA host; alive $PB joiner
shot $B policy-joiner-refused

kill_pair $PA $PB
assert_clean "$OUT/host.log" "$OUT/joiner.log"

# The host must have refused during the handshake (before WELCOME)...
grep -aq "net: identity - peer refused by policy" "$OUT/host.log" ||
  { echo "POLICY-E2E-FAIL: host never refused"; exit 1; }
# ...and the identity parse must have preceded the refusal (the policy got
# a real peer identity to judge).
grep -aq "net: identity peer name=" "$OUT/host.log" ||
  { echo "POLICY-E2E-FAIL: refusal without a parsed identity"; exit 1; }
# The refused joiner must never have received a world.
if grep -aq "bootstrap adopted" "$OUT/joiner.log"; then
  echo "POLICY-E2E-FAIL: refused joiner still bootstrapped"; exit 1
fi
echo "POLICY-E2E-OK"
