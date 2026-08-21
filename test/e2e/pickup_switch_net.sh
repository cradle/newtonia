#!/bin/bash
# Predicted pickup collection (needs a NETPLAY=1 build): a secondary
# pickup the JOINER flies over must switch its armed weapon AT the grab —
# predicted locally, like deploys and kills — not a round trip later via
# the snapshot restore.
#
# Without the prediction, the host's copy of the joiner's ship switched at
# collection while the pilot's ship switched only when the next restore
# arrived, and every press in that skew window deployed a type the other
# machine never fired — each one aged out of NET_DEPLOY_GRACE as "deploy
# dropped" (the load-gated arsenal divergence, 2026-08-21; root-caused
# from PR #490's CI artifacts and reproduced with this driver's hook).
#
# The host runs NEWTONIA_NET_TEST_MINE_PICKUP_MS: every 5 s it drops a
# MinePickup AHEAD of the joiner's ship (ahead, so a snapshot shows it to
# the client before contact — a pickup spawned ON the ship is collected
# host-side before the client ever sees it). The joiner fires a long
# missile burst through the drops.
#
# Asserts, on the joiner: at least one "pickup predicted" (a drop crossed
# its path and the local switch fired), missiles AND mines both confirmed
# by host echoes (the switch really happened on both machines), and NOT
# ONE deploy dropped — with the switch aligned in the press stream there
# is no mismatch window left on a quiet box. Prints PICKUP-SWITCH-E2E-OK.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

export NEWTONIA_ALL_WEAPONS=1
PA=$(launch host NEWTONIA_NET_TEST_MINE_PICKUP_MS=5000)
sleep 2
PB=$(launch joiner)
sleep 4

WINS=$(newtonia_windows)
A=$(echo "$WINS" | head -1); B=$(echo "$WINS" | tail -1)
[ "$A" != "$B" ] || { echo "only one window"; exit 1; }

nav_host $A
CODE=$(host_room_code host)
[ -n "$CODE" ] || { echo "NO ROOM CODE"; kill_pair $PA $PB; exit 1; }
echo "room code: $CODE"

nav_join $B "$CODE" joiner
echo "== waiting for connect"; sleep 18
alive $PA host; alive $PB joiner
grep -aq "bootstrap adopted" "$OUT/joiner.log" || { echo "NO BOOTSTRAP"; exit 1; }

# Arm missiles the way missile_net.sh does: fire one of whatever is armed,
# read what the host echoed back, cycle if it wasn't a missile.
select_missiles() {
  local before after i j
  for i in 1 2 3 4 5 6; do
    before=$(grep -ac "missile deploy confirmed" "$OUT/joiner.log")
    key $B x
    after=$before
    for j in 1 2 3 4 5 6 7 8; do
      sleep 1
      after=$(grep -ac "missile deploy confirmed" "$OUT/joiner.log")
      [ "$after" -gt "$before" ] && break
    done
    [ "$after" -gt "$before" ] && { echo "   (missiles armed after $i probe(s))"; return 0; }
    key $B c
    sleep 0.5
  done
  return 1
}
select_missiles || {
  echo "FAIL: could not arm MISSILES"
  kill_pair $PA $PB; exit 1; }

# A long press train flown THROUGH the pickup drops: thrust + rotate
# between rounds so the ship keeps crossing the spots the hook leads it
# with. The hook fires every 5 s (8 drops max), so ~30 s of flying gives
# several chances to cross one — the assert needs just one.
echo "== joiner: missile presses flown through the pickup drops"
for round in 1 2 3 4 5 6 7 8; do
  xdotool key --window $B --repeat 12 --repeat-delay 120 x
  key $B w; key $B w; key $B a
  alive $PB joiner
done
sleep 3

alive $PA host; alive $PB joiner
grep -aq "pickup predicted" "$OUT/joiner.log" || {
  echo "FAIL: the joiner never predicted a pickup — no drop crossed its"
  echo "      path (hook broken?) or the prediction pass never ran"
  kill_pair $PA $PB; exit 1; }
grep -aq "missile deploy confirmed" "$OUT/joiner.log" || {
  echo "FAIL: no missile deploys confirmed — the burst never fired"
  kill_pair $PA $PB; exit 1; }
grep -aq "mine deploy confirmed" "$OUT/joiner.log" || {
  echo "FAIL: pickup predicted but no mine deploys confirmed — the"
  echo "      switch happened on one machine only"
  kill_pair $PA $PB; exit 1; }
DROPPED=$(grep -ac "deploy dropped" "$OUT/joiner.log" || true)
[ "${DROPPED:-0}" -eq 0 ] || {
  echo "FAIL: $DROPPED deploy(s) dropped — the predicted switch left a"
  echo "      mismatch window in the press stream"
  grep -a "deploy dropped" "$OUT/joiner.log" | head -5
  kill_pair $PA $PB; exit 1; }
echo "== predicted: $(grep -ac 'pickup predicted' "$OUT/joiner.log"), reverts: $(grep -ac 'prediction expired' "$OUT/joiner.log")"

shot $A pickupswitch-host; shot $B pickupswitch-joiner
kill_pair $PA $PB
assert_clean "$OUT/host.log" "$OUT/joiner.log"
echo "PICKUP-SWITCH-E2E-OK"
