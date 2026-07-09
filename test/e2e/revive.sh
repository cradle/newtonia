#!/bin/bash
# Co-op revive regression (needs a NETPLAY=1 build): the joiner is emptied of
# lives (NEWTONIA_NET_TEST_KILL_MS) and starts spectating; the host
# spin-fires into the rocks — while a partner is fully out, every asteroid
# kill rolls 10% for a REVIVE pickup (one in the world at a time), so a
# spray burst must produce "revive pickup dropped". Then the
# NEWTONIA_NET_TEST_REVIVE_MS hook applies the revive payload (the
# collection effect without blind-navigating onto a pickup): the joiner's
# ship gets its last life back, its spectate ends, and no GAME OVER shows.
# Prints REVIVE-E2E-OK on success. See TESTING.md.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

# Joiner out at 20 s (post-connect), revive applied at 75 s.
PA=$(NEWTONIA_NET_TEST_KILL_MS=20000 NEWTONIA_NET_TEST_KILL_WHO=remote \
     NEWTONIA_NET_TEST_REVIVE_MS=90000 launch host)
sleep 2
PB=$(launch joiner)
sleep 4

WINS=$(newtonia_windows)
A=$(echo "$WINS" | head -1); B=$(echo "$WINS" | tail -1)
[ "$A" != "$B" ] || { echo "only one window"; exit 1; }

key $A Return; sleep 1; key $A s; key $A Return; sleep 1; key $A Return
CODE=$(host_room_code host)
[ -n "$CODE" ] || { echo "NO ROOM CODE"; kill $PA $PB; exit 1; }
echo "room code: $CODE"

key $B Return; sleep 1; key $B s; key $B Return; sleep 1; key $B s; key $B Return; sleep 1
for c in $(echo "$CODE" | grep -o .); do key $B "$c"; done
echo "== waiting for connect"; sleep 18
alive $PA host; alive $PB joiner
grep -aq "bootstrap adopted" "$OUT/joiner.log" || { echo "NO BOOTSTRAP"; exit 1; }

echo "== waiting for the joiner to be out of lives"
for i in $(seq 1 30); do
  grep -aq "TEST forcing remote player out" "$OUT/host.log" && break; sleep 1
done
grep -aq "TEST forcing remote player out" "$OUT/host.log" || {
  echo "KILL HOOK DID NOT FIRE"; kill $PA $PB; exit 1; }
sleep 2
grep -aq "spectate armed" "$OUT/joiner.log" || {
  echo "FAIL: joiner never armed spectate"; kill $PA $PB; exit 1; }

# Gen 0 can be nearly empty by now (the joiner's death took rocks with it) —
# skip two levels for a denser field. The rebuild's respawn is gated on
# alive||lives>0, so it does NOT resurrect the fully-out joiner.
key $A n; sleep 3; key $A n; sleep 3

# Host spin-fires into the rocks: rotation held while TAPPING fire (the
# level-0 gun is semi-auto — holding space is a single shot), spraying the
# whole circle so kills land despite blind aim.
echo "== host spin-fires for the revive drop"
xdotool keydown --window $A d
DROPPED=0
for i in $(seq 1 200); do
  xdotool key --window $A space
  sleep 0.2
  if [ $((i % 15)) = 0 ]; then
    grep -aq "revive pickup dropped" "$OUT/host.log" && { DROPPED=1; break; }
  fi
done
xdotool keyup --window $A d
grep -aq "revive pickup dropped" "$OUT/host.log" && DROPPED=1
[ "$DROPPED" = 1 ] || {
  echo "FAIL: no revive pickup dropped (partner out, 10%/kill, ~200 shots)"
  kill $PA $PB; exit 1; }
# (No count-based one-at-a-time assert: a level clear legitimately wipes
# the pickup and a replacement drops next generation, so drop lines CAN
# repeat across generations. The in-world cap is the inline pickup-list
# scan in the drop gate — structurally per-kill, same pickup list.)

echo "== waiting for the revive hook (90 s mark)"
for i in $(seq 1 70); do
  grep -aq "TEST applying revive" "$OUT/host.log" && break; sleep 1
done
grep -aq "TEST applying revive" "$OUT/host.log" || {
  echo "REVIVE HOOK DID NOT FIRE"; kill $PA $PB; exit 1; }
grep -aq "revive - partner respawning" "$OUT/host.log" || {
  echo "FAIL: revive applied but no fallen partner found"; kill $PA $PB; exit 1; }

# The joiner's lives replicate -> its spectate must end (camera back to its
# own ship, respawn countdown, then the alive-transition) and NO GAME OVER.
echo "== joiner should leave spectate"
for i in $(seq 1 15); do
  grep -aq "spectate ended" "$OUT/joiner.log" && break; sleep 1
done
grep -aq "spectate ended" "$OUT/joiner.log" || {
  echo "FAIL: joiner never left spectate after the revive"; kill $PA $PB; exit 1; }
grep -aq "game over" "$OUT/joiner.log" && {
  echo "FAIL: joiner hit game over across the revive"; kill $PA $PB; exit 1; }
sleep 6   # respawn countdown runs out; ship comes back via alive-transition
alive $PA host; alive $PB joiner
shot $B revive-joiner; shot $A revive-host

kill $PA $PB 2>/dev/null; wait $PA $PB 2>/dev/null
assert_clean "$OUT/host.log" "$OUT/joiner.log"
echo "REVIVE-E2E-OK"
