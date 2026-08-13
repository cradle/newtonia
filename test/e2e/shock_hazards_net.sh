#!/bin/bash
# PROTO 22 shock vs a mid-game hazard, online (needs a NETPLAY=1 build). This
# exercises the #142 client integration: a client-fired shock bolt must SEEK
# hostiles (not just asteroids) and stop() at survivors, with hull damage
# resolved host-side from the MSG_SHOCK polyline.
#
# The host skips to generation 9 — the PULSAR introduction. Pulsar is chosen
# deliberately: it only shoves ships (never lethal) so the joiner survives to
# keep firing, AND it is the survivor case — a shock arc that reaches it can't
# destroy it in one hit, so the client must stop() the bolt there (spark, no
# chain) while the host applies the hull hit. That stop()-at-survivor path is
# also the one that, if it left the struck pointer un-drained, dangled freed
# memory and segfaulted the joiner — so "both alive + clean logs" after firing
# in a pulsar field is the real regression guard. Both sides hold SHOCK (grant
# hook stocks it, add_shock leaves it selected) while spinning so the arcs
# sweep the pulsar. Asserts: bolts round-trip both ways, the pulsar replicated
# to the joiner, nobody crashed, logs clean. Prints SHOCK-HAZARDS-E2E-OK.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

# NEWTONIA_ALL_WEAPONS grants the full arsenal on EACH instance locally and
# auto-selects Shock (the last primary added) — the host-side GRANT_WEAPONS
# hook only stocks ammo, and weapon SELECTION is client-authoritative, so the
# joiner would otherwise stay on its default gun. (Flags the game cheated, fine
# for a wire test.)
export NEWTONIA_ALL_WEAPONS=1
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

echo "== host skips to generation 9 (the pulsar introduction)"
skip_to_generation $A host 9 || { echo "FAIL: host never reached generation 9"; exit 1; }
alive $PA host; alive $PB joiner
# Hold on gen 9 so the pulsar replicates to the joiner.
sleep 4
grep -aq "hazard replica spawned (kind 0)" "$OUT/joiner.log" || {
  echo "FAIL: joiner never replicated the pulsar"; kill $PA $PB; exit 1; }

# The grant hook re-stocks every primary and leaves SHOCK selected (add_shock
# splices it to the current slot), so DON'T cycle. Shock is SEMI-AUTOMATIC —
# one bolt per trigger pull — so each round below is a discrete press. Both
# spin (rotate held) so the arcs sweep the pulsar. Pulsar is non-lethal, so
# the joiner keeps firing.
echo "== both spin-fire shock across the pulsar field"
xdotool keydown --window $A d; xdotool keydown --window $B a
for round in $(seq 1 10); do
  xdotool keydown --window $A space; xdotool keydown --window $B space
  sleep 1
  xdotool keyup --window $A space; xdotool keyup --window $B space
  sleep 0.3
  alive $PA host || { echo "host died"; break; }
  alive $PB joiner || { echo "joiner died"; break; }
done
xdotool keyup --window $A d; xdotool keyup --window $B a

# Poll for the bolts to round-trip both ways (the remote-view MSG_SHOCK).
for i in $(seq 1 10); do
  grep -aq "shock bolt received" "$OUT/host.log" &&
  grep -aq "shock bolt received" "$OUT/joiner.log" && break
  sleep 1
done
grep -aq "shock bolt received" "$OUT/host.log" || {
  echo "FAIL: client shock never reached the host (MSG_SHOCK C->H)"
  kill $PA $PB; exit 1; }
grep -aq "shock bolt received" "$OUT/joiner.log" || {
  echo "FAIL: host shock never reached the client (MSG_SHOCK H->C)"
  kill $PA $PB; exit 1; }

alive $PA host; alive $PB joiner
shot $A shock-hazards-host; shot $B shock-hazards-joiner

kill $PA $PB 2>/dev/null; wait $PA $PB 2>/dev/null
assert_clean "$OUT/host.log" "$OUT/joiner.log"
echo "joiner hazard deaths:   $(grep -ac 'hazard replica destroyed' "$OUT/joiner.log")"
echo "SHOCK-HAZARDS-E2E-OK"
