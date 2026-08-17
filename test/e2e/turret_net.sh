#!/bin/bash
# Turret drones over the wire (needs a NETPLAY=1 build): the deployed sentry
# must fight for BOTH roles, and a client's deploy must survive the echo gap.
#
# The turret is the one deployable that goes on firing after the press: its
# bullets are minted by the machine that pilots the owner (the gun's
# is_local_player / net_remote_gun gate) and ride MSG_SHOT like gun shots,
# while the drone itself is host-echoed like a mine, deploy-grace held
# (Ship::NET_DEPLOY_GRACE) and vanish-detected on the client. Three ways
# that can rot, each asserted here:
#
#   1. The JOINER's deploy blows up at the muzzle — the mine/missile
#      muzzle-blast bug in turret clothes: a snapshot apply lands between
#      the press and the host's echo, and the vanish detection reads the
#      unmatched local copy as a host-side destruction. Signature: "turret
#      vanished" with essentially all of its 60 s left.
#   2. The JOINER's turret goes quiet — its firing sim runs but the bullets
#      never reach the host (the MSG_SHOT path) or never kill (the PROTO 13
#      claim path). NEITHER pilot touches a fire key in this driver, so any
#      reported-shot clone the host spawns, and any bullet-vs-asteroid
#      claim the joiner logs, can only be turret fire.
#   3. The HOST's turret is invisible/silent on the client — its bullets
#      must arrive as MSG_SHOT clones (the joiner's "reported shots/s
#      spawned" line) exactly like host gun fire.
#
# The HOST launches with NEWTONIA_NET_TEST_GRANT_TURRETS=1 (runtime hook,
# inert without the env var): both ships get the sentry as their ONLY
# secondary, so it is armed with no cycling — this driver's first version
# probed the secondary walk instead, and at generation 3 the idle joiner
# was out of lives before the walk ever reached TURRET. The pair still
# skips to generation 3 first (18 asteroids in a 2650^2 world) so a
# deployed turret reliably has a target inside its 900-unit range — and
# the deployed batteries then shoot the rocks that made that generation
# dangerous. Prints TURRET-E2E-OK on success. See TESTING.md.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

PA=$(launch host NEWTONIA_NET_TEST_GRANT_TURRETS=1)
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

# A generation with enough rocks that a turret dropped anywhere has a target
# in range — at generation 0 the world holds three asteroids and a turret
# can legitimately sit silent, which reads exactly like assertion 2.
echo "== host: skip to generation 3"
skip_to_generation $A host 3 || {
  echo "FAIL: host never reached generation 3"
  kill_pair $PA $PB; exit 1; }
sleep 3

# Deploy the joiner's battery: one press, then poll for the host's echo
# ("turret deploy confirmed" logs only on the joiner's own confirmed
# deploys). Retried, because a press can land during a respawn countdown
# and a dead ship deploys nothing.
echo "== joiner: deploy a battery"
deploy_confirmed() {  # deploy_confirmed WINDOW -> 0 once a fresh confirm lands
  local w=$1 before after i j
  for i in 1 2 3 4 5; do
    before=$(grep -ac "turret deploy confirmed" "$OUT/joiner.log")
    key "$w" x
    after=$before
    for j in 1 2 3 4 5 6; do
      sleep 1
      after=$(grep -ac "turret deploy confirmed" "$OUT/joiner.log")
      [ "$after" -gt "$before" ] && return 0
    done
  done
  return 1
}
deploy_confirmed $B || {
  echo "FAIL: no joiner turret deploy was ever confirmed by the host —"
  echo "      either the grant never replicated or the echo path is broken"
  kill_pair $PA $PB; exit 1; }
for i in 1 2 3 4; do
  key $B x
  key $B w
  alive $PB joiner
done

# Assertion 2, half one: the host spawns MSG_SHOT clones it can only have
# received from the joiner's turrets (1 Hz proof-of-flow line, so one line
# means a whole second of arriving turret fire).
echo "== waiting for the joiner's turret fire to reach the host"
OK=
for i in $(seq 1 45); do
  grep -aq "reported shots/s spawned" "$OUT/host.log" && { OK=1; break; }
  sleep 1
done
[ -n "$OK" ] || {
  echo "FAIL: the joiner's turrets never put a bullet on the host —"
  echo "      no MSG_SHOT clone was ever spawned there"
  kill_pair $PA $PB; exit 1; }
echo "   (host spawning clones of the joiner's turret fire)"

# Assertion 2, half two: those bullets hit things — the joiner detects its
# own bullet-vs-asteroid impacts locally and claims the kills (PROTO 13).
# Same attribution: no other client bullet exists in this driver.
OK=
for i in $(seq 1 45); do
  grep -aq "cosmetic impact" "$OUT/joiner.log" && { OK=1; break; }
  sleep 1
done
[ -n "$OK" ] || {
  echo "FAIL: the joiner's turret bullets never visibly hit an asteroid —"
  echo "      fire is flowing but the claim path never engaged"
  kill_pair $PA $PB; exit 1; }
echo "   (joiner's turret bullets hitting asteroids through the claim path)"

# Now the HOST deploys. Its own deploys are authoritative (nothing to
# confirm), so the observable is the deploy line itself — counted as a
# DELTA, because the host's log already carries one "turret deployed" per
# JOINER deploy (the INPUT press replays through the replica's weapon sim).
# The joiner is idle from here on, so any increment is the host's own.
echo "== host: deploy a battery"
host_deployed() {
  local before after i j
  for i in 1 2 3 4 5; do
    before=$(grep -ac "turret deployed" "$OUT/host.log")
    key $A x
    after=$before
    for j in 1 2 3 4; do
      sleep 1
      after=$(grep -ac "turret deployed" "$OUT/host.log")
      [ "$after" -gt "$before" ] && return 0
    done
  done
  return 1
}
host_deployed || {
  echo "FAIL: the host never landed a turret deploy of its own"
  kill_pair $PA $PB; exit 1; }
for i in 1 2 3; do key $A x; key $A w; done

# Assertion 3: the host's turret fire reaches the client as MSG_SHOT
# clones, the same 1 Hz line on the other log.
echo "== waiting for the host's turret fire to reach the joiner"
OK=
for i in $(seq 1 45); do
  grep -aq "reported shots/s spawned" "$OUT/joiner.log" && { OK=1; break; }
  sleep 1
done
[ -n "$OK" ] || {
  echo "FAIL: the host's turrets never put a clone on the joiner"
  kill_pair $PA $PB; exit 1; }
echo "   (joiner spawning clones of the host's turret fire)"

alive $PA host; alive $PB joiner

# Assertion 1: no muzzle blasts. A real host-side destruction (an asteroid
# or a hostile shot met the drone) happens after it has lived a while; the
# echo-gap bug vanishes the local copy within an apply or two of the press,
# i.e. with essentially all of LIFETIME_MS (60000) still on the clock. The
# line sits at 59500 — inside the ~400 ms grace-disabled signature. (Turrets
# survive their owner's respawn and are swept only by the level rollover's
# QUIET apply, so no legitimate wipe can produce a young vanish here.)
YOUNG=$(grep -a "turret vanished (host destruction)" "$OUT/joiner.log" |
        sed 's/.*life \([0-9]*\) ms.*/\1/' |
        awk '$1 >= 59500 { n++ } END { print n + 0 }')
[ "$YOUNG" -eq 0 ] || {
  echo "FAIL: $YOUNG joiner turret(s) vanished at the muzzle:"
  grep -a "turret vanished (host destruction)" "$OUT/joiner.log" | head -5
  kill_pair $PA $PB; exit 1; }

# ...and every deploy the joiner made reached the host's sim: a turret that
# ages out of the grace unconfirmed is one the host never fired.
DROPPED=$(grep -ac "turret deploy dropped" "$OUT/joiner.log" || true)
[ "${DROPPED:-0}" -eq 0 ] || {
  echo "FAIL: $DROPPED joiner turret deploy(s) never made it into the host's sim"
  kill_pair $PA $PB; exit 1; }

echo "== confirmed deploys: $(grep -ac 'turret deploy confirmed' "$OUT/joiner.log")"
shot $A turret-host; shot $B turret-joiner
kill_pair $PA $PB
assert_clean "$OUT/host.log" "$OUT/joiner.log"
echo "TURRET-E2E-OK"
