#!/bin/bash
# Client-fired deploy regression (needs a NETPLAY=1 build): a missile the
# JOINER launches must not blow up at the muzzle.
#
# Mines/gigas/missiles are deployed locally the instant the trigger is
# pulled, but the host owns their lifecycle and the client's snapshot
# rebuild replaces the list wholesale 10x/s. For the first apply or two
# the just-fired missile is missing from the host's set — which the
# vanish detection used to read as "the host detonated it": the client's
# own missile exploded in its face and the host's echo of the SAME
# missile then flew off normally (Glenn: "firing missiles as the client
# sometimes explode and shoot, like a double missile where one explodes
# instantly"). Ship::NET_DEPLOY_GRACE holds an unconfirmed deploy for a
# few applies instead.
#
# The host launches with NEWTONIA_ALL_WEAPONS=1 — weapons are host-owned,
# so its grant stocks BOTH ships and replicates to the joiner. The joiner
# cycles its secondary onto MISSILES and empties a long burst.
#
# Asserts, on the joiner: its launches were echoed back by the host
# ("missile deploy confirmed"), and NOT ONE missile vanished with most of
# its 3 s life left — a missile that never flew anywhere cannot have hit
# anything, so that line is the muzzle blast itself. Prints
# MISSILE-E2E-OK on success. See TESTING.md.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

export NEWTONIA_ALL_WEAPONS=1
PA=$(launch host)
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

nav_join $B "$CODE"
echo "== waiting for connect"; sleep 18
alive $PA host; alive $PB joiner
grep -aq "bootstrap adopted" "$OUT/joiner.log" || { echo "NO BOOTSTRAP"; exit 1; }

# give_all_weapons arms the everyday MINE and stocks the secondaries in
# add order (mine, giga mine, missile, shield, nova), so two cycles (c)
# reach MISSILES — and the confirm assert below fails loudly if that ever
# stops being true. Fire a few mines and gigas on the way through: they
# ride the same hold path, and a phantom boom on them is the same bug.
echo "== joiner: mines, then giga mines, then missiles"
for i in 1 2 3; do key $B x; done
key $B c
for i in 1 2 3; do key $B x; done
key $B c

# Long bursts of discrete presses (one missile per press — the secondary
# trigger has no auto-fire). The muzzle-blast race is per-launch and only
# fires when a snapshot apply lands in the window between the press and
# the host's echo of it, so the run needs volume: a handful of launches
# could pass on luck alone. ~120 here.
echo "== joiner: missile bursts (rotate + thrust between so they fly clear)"
for round in 1 2 3 4 5 6 7 8; do
  xdotool key --window $B --repeat 15 --repeat-delay 120 x
  sleep 1
  key $B w; key $B a
  alive $PB joiner
done
sleep 2

alive $PA host; alive $PB joiner
grep -aq "missile deploy confirmed" "$OUT/joiner.log" || {
  echo "FAIL: the joiner never landed a missile the host echoed back —"
  echo "      the burst never reached MISSILES, so nothing was tested"
  kill_pair $PA $PB; exit 1; }

# The regression itself. A host detonation happens where the missile flew
# to; a muzzle blast happens with the launch life (3000 ms) barely spent.
YOUNG=$(grep -a "missile vanished (host detonation)" "$OUT/joiner.log" |
        sed 's/.*life \([0-9]*\) ms.*/\1/' |
        awk '$1 >= 2500 { n++ } END { print n + 0 }')
[ "$YOUNG" -eq 0 ] || {
  echo "FAIL: $YOUNG client-fired missile(s) exploded at the muzzle:"
  grep -a "missile vanished (host detonation)" "$OUT/joiner.log" | head -5
  kill_pair $PA $PB; exit 1; }
echo "== no muzzle blasts; held deploys: $(grep -ac 'deploy held' "$OUT/joiner.log")"

shot $A missile-host; shot $B missile-joiner
kill_pair $PA $PB
assert_clean "$OUT/host.log" "$OUT/joiner.log"
echo "MISSILE-E2E-OK"
