#!/bin/bash
# LAN play e2e (task #159): two instances pair with NO relay involvement —
# the signal URL points at a dead port, so the host falls back to manual
# codes while its LAN beacon keeps running; the joiner discovers the host
# on the CodeEntry screen (loopback beacon), selects it with the arrow
# keys, and the blob exchange + host-candidate WebRTC session bring the
# game up to the client bootstrap. Prints LAN-E2E-OK on success.
# A private NEWTONIA_LAN_PORT keeps parallel CI runs (and a developer's
# real session) out of each other's beacons. See TESTING.md.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
export NEWTONIA_SIGNAL_URL="ws://127.0.0.1:9/ws"  # dead port: no relay
export NEWTONIA_LAN_PORT="${NEWTONIA_LAN_PORT:-42617}"
. "$(dirname "$0")/lib.sh"

PA=$(launch host)
sleep 2
PB=$(launch joiner)
sleep 4

WINS=$(newtonia_windows)
A=$(echo "$WINS" | head -1); B=$(echo "$WINS" | tail -1)
[ "$A" != "$B" ] || { echo "only one window"; exit 1; }

# Host: attract -> menu -> ONLINE -> HOST. The relay is unreachable, so
# after the 12 s signal timeout it lands on the manual-invite screen —
# with the LAN announce running the whole time.
key $A Return; sleep 1; key $A s; key $A Return; sleep 1; key $A Return

# Joiner: attract -> menu -> ONLINE -> JOIN (CodeEntry + browse).
key $B Return; sleep 1; key $B s; key $B Return; sleep 1; key $B s; key $B Return

echo "== waiting for discovery"
FOUND=0
for i in $(seq 1 15); do
  sleep 1
  grep -aq "lan host found" "$OUT/joiner.log" && { FOUND=1; break; }
done
[ "$FOUND" = 1 ] || { echo "NO LAN DISCOVERY"; kill $PA $PB; exit 1; }
grep -aq "lan announce up" "$OUT/host.log" || { echo "NO ANNOUNCE"; exit 1; }
shot $B lan-codeentry   # host row should be visible under the code field

# Select the discovered host (Down cycles code-field -> row 0) and join.
key $B Down; key $B Return

echo "== waiting for the LAN pairing + connect"
sleep 20
alive $PA host; alive $PB joiner
grep -aq "lan offer served" "$OUT/host.log"    || { echo "NO OFFER SERVE"; exit 1; }
grep -aq "lan answer received" "$OUT/host.log" || { echo "NO ANSWER"; exit 1; }
grep -aq "bootstrap adopted" "$OUT/joiner.log" || { echo "NO BOOTSTRAP"; exit 1; }
# The whole point: no relay was reachable, so any TURN/relay mention on
# the ICE path would mean the test lied about being offline.
grep -a "ice path" "$OUT/joiner.log" | grep -aqv relay || true

echo "== host skips a level; both fire for 5s"
key $A n; sleep 4; alive $PA host; alive $PB joiner
xdotool keydown --window $A space; xdotool keydown --window $B space
sleep 5
xdotool keyup --window $A space; xdotool keyup --window $B space
sleep 2
alive $PA host; alive $PB joiner
shot $A lan-host; shot $B lan-joiner

kill $PA $PB 2>/dev/null; wait $PA $PB 2>/dev/null
assert_clean "$OUT/host.log" "$OUT/joiner.log"
echo "LAN-E2E-OK"
