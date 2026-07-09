#!/bin/bash
# PROTO 18 regression: Pierce Beam + Lance online, on a normal NETPLAY=1
# build. NEWTONIA_NET_TEST_GRANT_WEAPONS=1 (runtime hook, inert without the
# env var) keeps every player stocked with Lance+Beam so the driver can
# cycle to them deterministically (real drops are random).
#
# Two instances connect via the relay; each side cycles off the default gun
# and fires the lance — the peer must log "lance pulse received" (MSG_LANCE,
# display flash + sound). Both then fire the beam (piercing MSG_SHOT clones
# ride the same path room.sh already asserts). Logs must stay clean: a
# broken per-bullet flags byte (PROTO 18 snapshot format) would show up as
# decode errors / NO BOOTSTRAP. Prints WEAPONS-E2E-OK on success.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

export NEWTONIA_NET_TEST_GRANT_WEAPONS=1
PA=$(launch host)
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

# DEBUG_BEAM stocks both primaries (PEW PEW + LANCE + BEAM; the grant
# auto-selects the last added). Fire a few shots at each of the three
# cycle positions on both sides (q = P1 next-weapon; each machine drives
# its LOCAL player with p1 keys) — every weapon fires at least once
# regardless of which one the grant left selected.
echo "== both fire through all three primaries (twice: a ship that died
==    mid-sequence respawns in ~4 s with the weapons re-granted)"
for round in 1 2 3 4 5 6; do
  for i in 1 2 3; do key $A space; key $B space; sleep 0.5; done
  key $A q; key $B q
  [ "$round" = 3 ] && sleep 5   # cover a respawn between the two passes
done
sleep 2
grep -aq "lance pulse received" "$OUT/host.log" || {
  echo "FAIL: client lance pulse never reached the host (MSG_LANCE C->H)"
  kill $PA $PB; exit 1; }
grep -aq "lance pulse received" "$OUT/joiner.log" || {
  echo "FAIL: host lance pulse never reached the client (MSG_LANCE H->C)"
  kill $PA $PB; exit 1; }
# Piercing/plain MSG_SHOT clones both ways (beam rides bit2): the
# proof-of-flow spawn counters cover delivery.
grep -aq "reported shots/s spawned" "$OUT/host.log" || {
  echo "FAIL: no client shot clones on the host"; kill $PA $PB; exit 1; }
grep -aq "reported shots/s spawned" "$OUT/joiner.log" || {
  echo "FAIL: no host shot clones on the client"; kill $PA $PB; exit 1; }

alive $PA host; alive $PB joiner
shot $A weapons-host; shot $B weapons-joiner

kill $PA $PB 2>/dev/null; wait $PA $PB 2>/dev/null
assert_clean "$OUT/host.log" "$OUT/joiner.log"
echo "WEAPONS-E2E-OK"
