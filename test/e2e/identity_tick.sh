#!/bin/bash
# The VERIFIED TICK on an attested badge (NETPLAY.md V0 polish). identity.sh
# proves an unattested claim renders as a role label and identity_attested.sh
# proves the worker's attestation reaches the game; this one goes one step
# further and checks what the PLAYER sees — the in-game HUD badge with the
# tick beside it, captured from a genuinely worker-attested session rather
# than a hand-made probe.
#
# Same shape as identity_attested.sh: self-hosts its own FAKE_VERIFY relay on
# a private port so the shared :8787 dev relay is untouched.
#
# WHAT IS ASSERTED automatically: both sides log the attestation, both badge
# bands contain ink (a blank band means the HUD or the badge regressed), and
# both games survive the capture. The tick's SHAPE is not asserted — pixel
# geometry checks would be brittle across layout changes — so the two PNGs
# are left in $OUT as the artifact to eyeball: expect "BOB - DESKTOP" on the
# host and "GLENN - DESKTOP" on the joiner, each with a checkmark after it.
# Prints TICK-E2E-OK. See TESTING.md.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi

HERE="$(cd "$(dirname "$0")" && pwd)"
PORT="${TICK_RELAY_PORT:-8790}"
export NEWTONIA_SIGNAL_URL="ws://127.0.0.1:$PORT/ws"

( cd "$HERE/../../signal" &&
  exec npx wrangler@4 dev --local --port "$PORT" --var FAKE_VERIFY:1 ) \
  > /tmp/tick_wrangler.log 2>&1 &
WPID=$!
trap 'kill $WPID 2>/dev/null' EXIT
echo "== starting FAKE_VERIFY relay on :$PORT (pid $WPID)"
for i in $(seq 1 90); do
  curl -s -o /dev/null "http://127.0.0.1:$PORT/" && break
  kill -0 $WPID 2>/dev/null || { echo "relay died:"; cat /tmp/tick_wrangler.log; exit 1; }
  sleep 1
done

. "$HERE/lib.sh"
relay_check

# badge_has_ink PNG NAME: the badge rows sit in the bottom fifth of the
# viewport (Overlay::net_badges). Fully black there = nothing drew.
badge_has_ink() {
  local band="$OUT/$2_badge.png" peak
  convert "$1" -gravity South -crop 100%x20%+0+0 +repage "$band" || return 1
  peak=$(convert "$band" -colorspace Gray -format "%[fx:maxima]" info:)
  awk -v q="$peak" 'BEGIN { exit !(q > 0.2) }'
}

pa=$(NEWTONIA_NET_NAME="GLENN" launch host);   sleep 2
pb=$(NEWTONIA_NET_NAME="BOB"   launch joiner); sleep 4

wins=$(newtonia_windows)
[ "$(echo "$wins" | wc -l)" -eq 2 ] ||
  { echo "expected 2 game windows, got: $wins"; kill_pair $pa $pb; exit 1; }
a=$(echo "$wins" | head -1); b=$(echo "$wins" | tail -1)

nav_host $a
code=$(host_room_code host)
[ -n "$code" ] || { echo "NO ROOM CODE"; kill_pair $pa $pb; exit 1; }
echo "room code: $code"

nav_join $b "$code"
echo "== waiting for connect + attestation"; sleep 18
alive $pa host; alive $pb joiner

# Only an ATTESTED peer earns the tick, so the badge is only worth shooting
# once the attestation has actually landed on both sides.
grep -aq "net: identity attested name='GLENN' platform=DESKTOP(1)" "$OUT/joiner.log" ||
  { echo "TICK-E2E-FAIL: joiner never logged the host's attestation";
    kill_pair $pa $pb; exit 1; }
grep -aq "net: identity attested name='BOB' platform=DESKTOP(1)" "$OUT/host.log" ||
  { echo "TICK-E2E-FAIL: host never logged the joiner's attestation";
    kill_pair $pa $pb; exit 1; }

# The badge names the REMOTE peer: the host sees BOB, the joiner sees GLENN.
echo "== both sides attested; capturing the HUD badge"
shot $a host_sees_bob
shot $b joiner_sees_glenn
alive $pa host; alive $pb joiner

for pair in "host_sees_bob host" "joiner_sees_glenn joiner"; do
  set -- $pair
  [ -f "$OUT/$1.png" ] ||
    { echo "TICK-E2E-FAIL: no screenshot for $2"; kill_pair $pa $pb; exit 1; }
  badge_has_ink "$OUT/$1.png" "$1" ||
    { echo "TICK-E2E-FAIL: $2's badge band is blank (HUD or badge regressed)";
      kill_pair $pa $pb; exit 1; }
done

kill_pair $pa $pb
assert_clean "$OUT/host.log" "$OUT/joiner.log"
wait_no_windows
echo "badge shots: $OUT/host_sees_bob.png $OUT/joiner_sees_glenn.png"
echo "  (eyeball the tick: 'BOB - DESKTOP <tick>' / 'GLENN - DESKTOP <tick>')"
echo "TICK-E2E-OK"
