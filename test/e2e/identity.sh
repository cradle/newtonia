#!/bin/bash
# Peer-identity happy path: two current builds connect via a room code and
# each side must log the OTHER's identity from the HELLO/WELCOME append
# ("net: identity peer name='PLAYER' platform=DESKTOP(1)" — the default
# backend's generic name + compile-time platform). Guards the identity
# exchange end-to-end (net_identity.*, net_session.cpp). Prints
# IDENTITY-E2E-OK. See TESTING.md.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

PA=$(launch host)
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

nav_join $B "$CODE"
echo "== waiting for connect"; sleep 18
alive $PA host; alive $PB joiner
grep -aq "bootstrap adopted" "$OUT/joiner.log" || { echo "NO BOOTSTRAP"; exit 1; }

kill_pair $PA $PB
assert_clean "$OUT/host.log" "$OUT/joiner.log"

# The host learns the client's identity from HELLO; the client learns the
# host's from WELCOME. Both sides of this run are the same desktop build.
grep -aq "net: identity peer name='PLAYER' platform=DESKTOP(1)" "$OUT/host.log" ||
  { echo "IDENTITY-E2E-FAIL: host never logged the client identity"; exit 1; }
grep -aq "net: identity peer name='PLAYER' platform=DESKTOP(1)" "$OUT/joiner.log" ||
  { echo "IDENTITY-E2E-FAIL: joiner never logged the host identity"; exit 1; }
echo "IDENTITY-E2E-OK"
