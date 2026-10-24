#!/bin/bash
# Per-peer platform policy (net_policy.h net_peer_verdict), the hook a
# console's block-list check rides: the default backend's inert test hook
# NEWTONIA_NET_TEST_PEER_VERDICT=refuse|wait drives each end of it.
#   host refuse  the host refuses INSIDE the handshake (MSG_REJECT
#                RejectNotAllowed); the joiner never bootstraps.
#   host wait    the held WELCOME times out into the same refusal.
#   joiner refuse  the joiner leaves before entering the host's game.
# Neither side may crash. Prints POLICY-PEER-E2E-OK. See TESTING.md.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

# run_case NAME HOST_VERDICT JOINER_VERDICT
run_case() {
  local name=$1 hv=$2 jv=$3 PA PB WINS A B CODE
  PA=$(launch "$name-host" NEWTONIA_NET_TEST_PEER_VERDICT=$hv)
  sleep 2
  PB=$(launch "$name-joiner" NEWTONIA_NET_TEST_PEER_VERDICT=$jv)
  sleep 4
  WINS=$(newtonia_windows)
  [ "$(echo "$WINS" | wc -l)" -eq 2 ] ||
    { echo "expected 2 game windows, got: $WINS"; kill_pair $PA $PB; exit 1; }
  A=$(echo "$WINS" | head -1); B=$(echo "$WINS" | tail -1)
  nav_host $A
  CODE=$(host_room_code "$name-host")
  [ -n "$CODE" ] || { echo "NO ROOM CODE ($name)"; kill_pair $PA $PB; exit 1; }
  echo "== $name: room code $CODE"
  nav_join $B "$CODE" "$name-joiner"
  sleep 16
  alive $PA "$name host"; alive $PB "$name joiner"
  shot $B "policy-peer-$name"
  kill_pair $PA $PB
  assert_clean "$OUT/$name-host.log" "$OUT/$name-joiner.log"
  if grep -aq "bootstrap adopted" "$OUT/$name-joiner.log"; then
    echo "POLICY-PEER-E2E-FAIL: $name: refused joiner still bootstrapped"; exit 1
  fi
}

run_case hostrefuse refuse allow
grep -aq "net: refusing not-allowed pilot at the handshake" "$OUT/hostrefuse-host.log" ||
  { echo "POLICY-PEER-E2E-FAIL: host never refused"; exit 1; }
grep -aq "net: host refused this connection (reason" "$OUT/hostrefuse-joiner.log" ||
  { echo "POLICY-PEER-E2E-FAIL: joiner never heard the refusal"; exit 1; }

run_case hostwait wait allow
grep -aq "net: refusing not-allowed pilot at the handshake" "$OUT/hostwait-host.log" ||
  { echo "POLICY-PEER-E2E-FAIL: held WELCOME never timed out into a refusal"; exit 1; }

run_case joinerrefuse allow refuse
grep -aq "\[lobby\] host refused by platform policy (refused)" "$OUT/joinerrefuse-joiner.log" ||
  { echo "POLICY-PEER-E2E-FAIL: joiner never refused the host"; exit 1; }

echo "POLICY-PEER-E2E-OK"
