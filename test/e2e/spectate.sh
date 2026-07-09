#!/bin/bash
# Netplay spectator-mode regression: two instances connect, the host is
# told (NEWTONIA_NET_TEST_KILL_MS) to empty the JOINER's lives while it
# keeps playing. The joiner should show a 5 s "SPECTATING IN N" countdown
# on its own wreck, then hand the camera to the host and show "SPECTATING"
# — never the GAME OVER card (the host is still in it). Prints
# SPECTATE-E2E-OK on success. See TESTING.md.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

# Host empties a player's lives 20 s into 2-player play — comfortably after
# the connect handshake below, so we can catch the countdown live. WHO
# defaults to "remote" (the joiner spectates the host); "local" empties the
# host's own lives so the HOST spectates the joiner. SPECTATOR points the
# capture at whichever window belongs to the player who went out.
WHO="${SPECTATE_WHO:-remote}"
PA=$(NEWTONIA_NET_TEST_KILL_MS=20000 NEWTONIA_NET_TEST_KILL_WHO=$WHO launch host)
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

# Wait for the kill hook to fire, then burst-capture the joiner: the 5 s
# "SPECTATING IN N" countdown, then the "SPECTATING" hand-off.
echo "== waiting for kill hook"
for i in $(seq 1 30); do
  grep -aq "TEST forcing $WHO player out" "$OUT/host.log" && break
  sleep 1
done
grep -aq "TEST forcing $WHO player out" "$OUT/host.log" || {
  echo "KILL HOOK DID NOT FIRE"; kill $PA $PB; exit 1; }
# Screenshot the window of whoever went out (the spectator) across the 5 s
# countdown + into spectating; the other window is the peer still playing.
if [ "$WHO" = local ]; then SPEC=$A; PEER=$B; else SPEC=$B; PEER=$A; fi
for s in 1 2 3 4 5 6 7; do shot $SPEC "spectate-t$s"; done
cp "$OUT/spectate-t2.png" "$OUT/spectate-countdown.png"  # mid-countdown
cp "$OUT/spectate-t7.png" "$OUT/spectate-active.png"     # after hand-off
shot $PEER spectate-peer        # peer view unaffected (still playing)

kill $PA $PB 2>/dev/null; wait $PA $PB 2>/dev/null
assert_clean "$OUT/host.log" "$OUT/joiner.log"
echo "SPECTATE-E2E-OK"
