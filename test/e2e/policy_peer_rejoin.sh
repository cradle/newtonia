#!/bin/bash
# The joiner's per-peer platform verdict (net_policy.h net_peer_verdict)
# across a paused-host reconnect. The host pauses when the joiner drops
# and sends a rejoiner ONE keyframe, so a joiner still waiting on its
# verdict must hold that snapshot rather than wait for another.
# The hiccup.sh freeze/thaw drops the transport; the default backend's
# hook NEWTONIA_NET_TEST_PEER_VERDICT allows the first join and WAITs on
# the rejoin:
#   allow      rejoin-wait:2500  the held keyframe bootstraps once the
#                                verdict turns ALLOW (no new snapshot).
#   timeout    rejoin-wait       the wait expires into a refusal; no
#                                second bootstrap.
# Prints POLICY-PEER-REJOIN-E2E-OK. See TESTING.md.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

fail() { echo "POLICY-PEER-REJOIN-E2E-FAIL: $*"; kill -CONT $PB 2>/dev/null; kill_pair $PA $PB; exit 1; }

# run_case NAME JOINER_VERDICT
run_case() {
  local name=$1 jv=$2 WINS A B CODE LOST i
  PA=$(launch "$name-host"); sleep 2
  PB=$(launch "$name-joiner" NEWTONIA_NET_TEST_PEER_VERDICT=$jv); sleep 4
  WINS=$(newtonia_windows)
  [ "$(echo "$WINS" | wc -l)" -eq 2 ] || fail "$name: expected 2 game windows, got: $WINS"
  A=$(echo "$WINS" | head -1); B=$(echo "$WINS" | tail -1)
  nav_host $A
  CODE=$(host_room_code "$name-host")
  [ -n "$CODE" ] || fail "$name: no room code"
  echo "== $name: room code $CODE"
  nav_join $B "$CODE" "$name-joiner"
  for i in $(seq 1 22); do
    grep -aq "bootstrap adopted" "$OUT/$name-joiner.log" && break; sleep 1
  done
  grep -aq "bootstrap adopted" "$OUT/$name-joiner.log" || fail "$name: no first bootstrap"

  echo "== $name: freeze the joiner until the host's transport fails"
  kill -STOP $PB
  LOST=""
  for i in $(seq 1 60); do
    sleep 1; alive $PA "$name host"
    grep -aq "player 2 lost" "$OUT/$name-host.log" && { LOST=1; break; }
  done
  [ -n "$LOST" ] || fail "$name: host never detected the loss"
  grep -aq "paused awaiting rejoin" "$OUT/$name-host.log" || fail "$name: no auto-pause"
  kill -CONT $PB

  for i in $(seq 1 45); do
    sleep 1; alive $PA "$name host"; alive $PB "$name joiner"
    grep -aq "platform allowed the host after\|host refused by platform policy" \
      "$OUT/$name-joiner.log" && break
  done
  sleep 3
  alive $PA "$name host"; alive $PB "$name joiner"
  shot $B "policy-peer-rejoin-$name"
  kill_pair $PA $PB
  assert_clean "$OUT/$name-host.log" "$OUT/$name-joiner.log"
  grep -aq "holding the bootstrap for the platform's verdict" "$OUT/$name-joiner.log" ||
    fail "$name: the rejoin never held its bootstrap"
}

run_case allow rejoin-wait:2500
grep -aq "platform allowed the host after" "$OUT/allow-joiner.log" ||
  fail "allow: the held keyframe never bootstrapped"
[ "$(grep -ac 'bootstrap adopted' "$OUT/allow-joiner.log")" -ge 2 ] ||
  fail "allow: no second bootstrap"

run_case timeout rejoin-wait
grep -aq "host refused by platform policy (no verdict)" "$OUT/timeout-joiner.log" ||
  fail "timeout: the wait never expired into a refusal"
[ "$(grep -ac 'bootstrap adopted' "$OUT/timeout-joiner.log")" -eq 1 ] ||
  fail "timeout: bootstrapped despite no verdict"

echo "POLICY-PEER-REJOIN-E2E-OK"
