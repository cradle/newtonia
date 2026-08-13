#!/bin/bash
# Peer-identity ATTESTATION happy path (NETPLAY.md V0/V1). Unlike identity.sh
# (which proves an unverified claim renders as a role label), this drives a
# worker that VERIFIES: a local wrangler dev with FAKE_VERIFY=1 stands in for
# the Steam backend and attests each side's claimed identity. Both peers must
# then log the greppable attestation line
#   "net: identity attested name='GLENN' platform=DESKTOP(1)"
# proving the worker's `identity` broadcast reached the game and was folded in
# as an ATTESTED field (which renders online — see net_identity.h). Self-hosts
# its own FAKE_VERIFY relay on a private port so the shared :8787 dev relay
# (plain, used by every other driver) is untouched. Prints ATTEST-E2E-OK.
# See TESTING.md.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi

HERE="$(cd "$(dirname "$0")" && pwd)"
PORT="${ATTEST_RELAY_PORT:-8788}"
export NEWTONIA_SIGNAL_URL="ws://127.0.0.1:$PORT/ws"

# A private FAKE_VERIFY relay: the dev flag attests the claimed identity
# without contacting any platform backend (never set in production).
( cd "$HERE/../../signal" &&
  exec npx wrangler@4 dev --local --port "$PORT" --var FAKE_VERIFY:1 ) \
  > /tmp/attest_wrangler.log 2>&1 &
WPID=$!
trap 'kill $WPID 2>/dev/null' EXIT
echo "== starting FAKE_VERIFY relay on :$PORT (pid $WPID)"
for i in $(seq 1 90); do
  curl -s -o /dev/null "http://127.0.0.1:$PORT/" && break
  kill -0 $WPID 2>/dev/null || { echo "relay died:"; cat /tmp/attest_wrangler.log; exit 1; }
  sleep 1
done

. "$HERE/lib.sh"
relay_check

# One host+joiner run to a connected game (named both ways: the host is
# GLENN, the joiner BOB), then assert the attestation logs.
pa=$(NEWTONIA_NET_NAME="GLENN" launch host)
sleep 2
pb=$(NEWTONIA_NET_NAME="BOB" launch joiner)
sleep 4

wins=$(newtonia_windows)
[ "$(echo "$wins" | wc -l)" -eq 2 ] ||
  { echo "expected 2 game windows, got: $wins"; kill_pair $pa $pb; exit 1; }
a=$(echo "$wins" | head -1); b=$(echo "$wins" | tail -1)

nav_host $a
code=$(host_room_code host)
[ -n "$code" ] || { echo "NO ROOM CODE"; kill_pair $pa $pb; exit 1; }
echo "room code: $code"

nav_join $b "$code" joiner
echo "== waiting for connect + attestation"; sleep 18
alive $pa host; alive $pb joiner
grep -aq "bootstrap adopted" "$OUT/joiner.log" ||
  { echo "NO BOOTSTRAP (joiner)"; kill_pair $pa $pb; exit 1; }

# The worker verified both sides (FAKE_VERIFY) and broadcast each identity to
# the peer; the game folded it in as ATTESTED and logged it. The joiner learns
# the host's attested identity; the host learns the joiner's.
grep -aq "net: identity attested name='GLENN' platform=DESKTOP(1)" "$OUT/joiner.log" ||
  { echo "ATTEST-E2E-FAIL: joiner never logged the host's attestation";
    kill_pair $pa $pb; exit 1; }
grep -aq "net: identity attested name='BOB' platform=DESKTOP(1)" "$OUT/host.log" ||
  { echo "ATTEST-E2E-FAIL: host never logged the joiner's attestation";
    kill_pair $pa $pb; exit 1; }

kill_pair $pa $pb
assert_clean "$OUT/host.log" "$OUT/joiner.log"
wait_no_windows
echo "ATTEST-E2E-OK"
