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
A=$(echo "$WINS" | head -1); B=$(echo "$WINS" | tail -1)
[ "$A" != "$B" ] || { echo "only one window"; exit 1; }

# Host: attract -> menu -> ONLINE -> HOST
key $A Return; sleep 1; key $A s; key $A Return; sleep 1; key $A Return

CODE=$(host_room_code host)
[ -n "$CODE" ] || { echo "NO ROOM CODE"; kill $PA $PB; exit 1; }
echo "room code: $CODE"

# Joiner: attract -> menu -> ONLINE -> JOIN -> type the code
key $B Return; sleep 1; key $B s; key $B Return; sleep 1; key $B s; key $B Return; sleep 1
for c in $(echo "$CODE" | grep -o .); do key $B "$c"; done
echo "== waiting for connect"; sleep 18
alive $PA host; alive $PB joiner
grep -aq "bootstrap adopted" "$OUT/joiner.log" || { echo "NO BOOTSTRAP"; exit 1; }

kill $PA $PB 2>/dev/null; wait $PA $PB 2>/dev/null
assert_clean "$OUT/host.log" "$OUT/joiner.log"

# The host learns the client's identity from HELLO; the client learns the
# host's from WELCOME. Both sides of this run are the same desktop build.
grep -aq "net: identity peer name='PLAYER' platform=DESKTOP(1)" "$OUT/host.log" ||
  { echo "IDENTITY-E2E-FAIL: host never logged the client identity"; exit 1; }
grep -aq "net: identity peer name='PLAYER' platform=DESKTOP(1)" "$OUT/joiner.log" ||
  { echo "IDENTITY-E2E-FAIL: joiner never logged the host identity"; exit 1; }
echo "IDENTITY-E2E-OK"
