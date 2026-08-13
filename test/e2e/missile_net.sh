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

# ...and CONFIRM the cursor actually landed on missiles before firing 120 of
# them. Two `c` presses reach missiles only if both land; a dropped one leaves
# the joiner on giga mines, the whole burst deploys the wrong weapon, and the
# driver's verdict is "the burst never reached MISSILES, so nothing was
# tested" — true, and no help in saying why (twice on CI, 2026-08-12).
# Each deploy kind logs its own confirm line, so the selection is observable:
# fire one, see what came back, cycle if it wasn't a missile.
select_missiles() {
  local before after i j
  for i in 1 2 3 4 5 6; do
    before=$(grep -ac "missile deploy confirmed" "$OUT/joiner.log")
    key $B x                       # fire one of whatever is armed
    # Poll for the host's echo rather than sampling once after a fixed wait:
    # a flat 2 s was enough on a quiet box and not on a loaded runner, where
    # a slow confirm read as "not missiles", cycled the selection PAST them,
    # and the probe gave up having walked the whole list (2026-08-13).
    after=$before
    for j in 1 2 3 4 5 6 7 8; do
      sleep 1
      after=$(grep -ac "missile deploy confirmed" "$OUT/joiner.log")
      [ "$after" -gt "$before" ] && break
    done
    [ "$after" -gt "$before" ] && { echo "   (missiles armed after $i probe(s))"; return 0; }
    key $B c                       # not missiles — next secondary
    sleep 0.5
  done
  return 1
}
select_missiles || {
  echo "FAIL: could not arm MISSILES — cycled the whole secondary list without"
  echo "      a single missile deploy confirmed by the host"
  kill_pair $PA $PB; exit 1; }

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

# Batched presses. SIGSTOP the joiner and the presses pile up in X; on
# SIGCONT it drains all three in ONE frame, so they reach the host in a
# single INPUT — the same shape a lost-packet stall delivers on recovery,
# and the only way to produce it deterministically against a local relay
# (xdotool cannot outrun the 8 ms input send). Held well under the 1 s
# dead-man switch, and under the 4-per-INPUT clamp the wrap counters use.
#
# The host queues them (Ship::net_queued_secondary_presses) and replays
# one per step, exactly as it already did for batched primary presses.
# Firing once for the whole batch made fewer missiles than the client
# launched, and the extra local copies then aged out with no echo to
# confirm them: 12 of 18 launches lost in this phase before the queue.
echo "== joiner: batched presses (client stopped, then resumed)"
for round in 1 2 3 4 5 6; do
  kill -STOP $PB
  for i in 1 2 3; do xdotool key --window $B x; done
  kill -CONT $PB
  sleep 1.5
  key $B a
  alive $PB joiner
done
sleep 3

alive $PA host; alive $PB joiner
grep -aq "missile deploy confirmed" "$OUT/joiner.log" || {
  echo "FAIL: the joiner never landed a missile the host echoed back —"
  echo "      the burst never reached MISSILES, so nothing was tested"
  kill_pair $PA $PB; exit 1; }

# The regression itself. A host detonation happens where the missile flew
# to — even a point-blank one has a few hundred ms of its 3000 on the
# clock. A muzzle blast happens within an apply or two of the launch, so
# the bug's signature is "vanished having flown under 200 ms": every one
# of them logs 2990-3000 with the grace forced to 0, while the closest
# real detonation seen in these runs was 2576.
YOUNG=$(grep -a "missile vanished (host detonation)" "$OUT/joiner.log" |
        sed 's/.*life \([0-9]*\) ms.*/\1/' |
        awk '$1 >= 2800 { n++ } END { print n + 0 }')
[ "$YOUNG" -eq 0 ] || {
  echo "FAIL: $YOUNG client-fired missile(s) exploded at the muzzle:"
  grep -a "missile vanished (host detonation)" "$OUT/joiner.log" | head -5
  kill_pair $PA $PB; exit 1; }
echo "== no muzzle blasts; held deploys: $(grep -ac 'deploy held' "$OUT/joiner.log")"

# Every launch the client made must have been made by the host too: a
# deploy that ages out of the grace with no echo is one the host never
# fired, which is what collapsing a batch of presses used to cause.
DROPPED=$(grep -ac "deploy dropped" "$OUT/joiner.log" || true)
[ "${DROPPED:-0}" -eq 0 ] || {
  echo "FAIL: $DROPPED client deploy(s) never made it into the host's sim —"
  echo "      batched secondary presses are being collapsed host-side"
  kill_pair $PA $PB; exit 1; }

shot $A missile-host; shot $B missile-joiner
kill_pair $PA $PB
assert_clean "$OUT/host.log" "$OUT/joiner.log"
echo "MISSILE-E2E-OK"
