#!/bin/bash
# ALLOW ANONYMOUS PLAYERS = NO (FOURPLAYER.md O3). The host's admission
# policy, set from the waiting room's own row: only players the signalling
# worker vouched for may take a seat.
#
# The whole test hinges on the difference between the two kinds of joiner,
# so it drives its own FAKE_VERIFY relay (identity_attested.sh's pattern):
# a NAMED pilot gets attested and must be seated, a NAMELESS one cannot be
# and must be refused. Against the plain :8787 dev relay nobody is attested
# and the first half would fail for the wrong reason.
#
# Also proves the grace window: the worker's verdict is async and normally
# lands AFTER the handshake, so a naive check would refuse the named pilot
# too (that is why PendingJoiner::anon_wait_ms exists).
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
cd "$(dirname "$0")"

PORT="${ANON_RELAY_PORT:-8790}"
export NEWTONIA_SIGNAL_URL="ws://127.0.0.1:$PORT/ws"
( cd ../../signal &&
  exec npx wrangler@4 dev --local --port "$PORT" --var FAKE_VERIFY:1 ) \
  > /tmp/nseat_anon_wrangler.log 2>&1 &
WPID=$!
# kill_tree, not a bare kill: TERMing the npx wrapper leaves its node/
# workerd children orphaned and holding the port (lib.sh). The plain-kill
# fallback covers an exit before lib.sh is sourced (the relay-died path).
trap 'kill_tree $WPID 2>/dev/null || kill $WPID 2>/dev/null' EXIT
echo "== starting FAKE_VERIFY relay on :$PORT (pid $WPID)"
for _ in $(seq 1 90); do
  curl -s -o /dev/null "http://127.0.0.1:$PORT/" && break
  kill -0 $WPID 2>/dev/null || { echo "relay died:"; cat /tmp/nseat_anon_wrangler.log; exit 1; }
  sleep 1
done

. ./lib.sh
relay_check
export NEWTONIA_NET_TEST_SEATS=4   # a room that stays open, not one that starts

ROOM_PIDS=(); ROOM_WINS=(); ROOM_CODE=
ROOM_PIDS[0]=$(launch host)
sleep 3
ROOM_WINS[0]=$(newtonia_windows | head -1)
[ -n "${ROOM_WINS[0]}" ] || room_fail "NO HOST WINDOW" host
HW=${ROOM_WINS[0]}
nav_host "$HW"
ROOM_CODE=$(host_room_code host)
[ -n "$ROOM_CODE" ] || room_fail "NO ROOM CODE" host
echo "room code: $ROOM_CODE"

echo "== host: turn ALLOW ANONYMOUS PLAYERS off"
# With nobody seated the policy row is the ONLY row, so it already has the
# cursor — no stepping needed. Right toggles it (so does Enter).
xdotool key --window "$HW" d; sleep 1
ok=
for _ in $(seq 1 10); do
  grep -aq "allow anonymous players: NO" "$OUT/host.log" && { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "POLICY NEVER CHANGED" host

echo "== a pilot the worker vouches for is seated"
before=$(newtonia_windows)
ROOM_PIDS[1]=$(launch named NEWTONIA_NET_NAME=PILOT1)
sleep 4
W=$(new_window_since "$before")
[ -n "$W" ] || room_fail "NO WINDOW FOR THE NAMED PILOT" named
nav_join "$W" "$ROOM_CODE" named
ok=
for _ in $(seq 1 40); do
  grep -aq "seat 2 filled" "$OUT/host.log" && { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "ATTESTED PILOT WAS NOT SEATED" host

echo "== a pilot with no name at all is refused"
# No NEWTONIA_NET_NAME: the claim carries an empty name, so the worker
# attests the platform and nothing else — anonymous by the same rule that
# decides whether BAN is offered (net_identity_anonymous).
before=$(newtonia_windows)
ROOM_PIDS[2]=$(launch anon)
sleep 4
W=$(new_window_since "$before")
[ -n "$W" ] || room_fail "NO WINDOW FOR THE ANONYMOUS PILOT" anon
nav_join "$W" "$ROOM_CODE" anon
ok=
for _ in $(seq 1 40); do
  grep -aq "refusing anonymous pilot" "$OUT/host.log" && { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "ANONYMOUS PILOT WAS NOT REFUSED" host
grep -aq "seat 3 filled" "$OUT/host.log" &&
  room_fail "ANONYMOUS PILOT GOT A SEAT" host

room_alive
room_kill_all
echo "NSEAT-ANON-OK (attested seated, anonymous refused)"
