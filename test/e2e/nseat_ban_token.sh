#!/bin/bash
# Account-token bans (FOURPLAYER.md O3): a ban keys on the worker's
# room-scoped account token, so a display-name change no longer evades it.
#
# Needs attestation, so it drives its own FAKE_VERIFY relay (nseat_anon's
# pattern), where the CREDENTIAL doubles as the fake account id
# (NEWTONIA_NET_TEST_CRED, the dev hook in net_identity.cpp): two joins
# under one cred are one "account" wearing two names — exactly the rename
# bypass this exists to close. ALLOW ANONYMOUS is turned OFF first so every
# admit waits for the attestation (AdmitWait): the banned verdict is then
# deterministic at the door instead of racing the seating (the late-
# attestation sweep covers that race in the live default, but a test that
# sometimes takes each path asserts neither).
#
# The four beats:
#   1. MALLORY (cred ACCT1) is attested, seated, then BANNED — the [ban]
#      log must record a token, not just the name;
#   2. the same account back under a NEW name (MALLORY2, cred ACCT1) is
#      REFUSED and told why (reason 4) — the rename bypass, closed;
#   3. a DIFFERENT account (CLEAN, cred ACCT2) still seats — the ban took
#      the account, not the room;
#   4. the host survives it all.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
cd "$(dirname "$0")"

PORT="${BAN_TOKEN_RELAY_PORT:-8792}"
export NEWTONIA_SIGNAL_URL="ws://127.0.0.1:$PORT/ws"
( cd ../../signal &&
  exec npx wrangler@4 dev --local --port "$PORT" --var FAKE_VERIFY:1 ) \
  > /tmp/nseat_ban_token_wrangler.log 2>&1 &
WPID=$!
# kill_tree, not a bare kill: TERMing the npx wrapper leaves its node/
# workerd children orphaned and holding the port (lib.sh). The plain-kill
# fallback covers an exit before lib.sh is sourced (the relay-died path).
trap 'kill_tree $WPID 2>/dev/null || kill $WPID 2>/dev/null' EXIT
echo "== starting FAKE_VERIFY relay on :$PORT (pid $WPID)"
for _ in $(seq 1 90); do
  curl -s -o /dev/null "http://127.0.0.1:$PORT/" && break
  kill -0 $WPID 2>/dev/null || { echo "relay died:"; cat /tmp/nseat_ban_token_wrangler.log; exit 1; }
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

echo "== host: ALLOW ANONYMOUS off (the admit then waits for attestation)"
# With nobody seated the policy row is the ONLY row, so it already has the
# cursor — right toggles it (nseat_anon's own first step).
xdotool key --window "$HW" d; sleep 1
ok=
for _ in $(seq 1 10); do
  grep -aq "allow anonymous players: NO" "$OUT/host.log" && { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "POLICY NEVER CHANGED" host

echo "== MALLORY (account ACCT1) is attested and seated"
before=$(newtonia_windows)
ROOM_PIDS[1]=$(launch mallory NEWTONIA_NET_NAME=MALLORY NEWTONIA_NET_TEST_CRED=ACCT1)
sleep 4
W=$(new_window_since "$before")
[ -n "$W" ] || room_fail "NO WINDOW FOR MALLORY" mallory
nav_join "$W" "$ROOM_CODE" mallory
ok=
for _ in $(seq 1 40); do
  grep -aq "seat 2 filled" "$OUT/host.log" && { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "MALLORY WAS NOT SEATED" host

echo "== host bans the seat, and the ban records the account token"
# The list rests on START GAME under the one peer row: UP lands on it,
# RIGHT swaps the action to BAN (offered — the pilot is attested), Enter
# arms, Enter performs.
xdotool key --window "$HW" w; sleep 0.4
xdotool key --window "$HW" d; sleep 0.4
xdotool key --window "$HW" Return; sleep 0.5   # arm ([CONFIRM BAN])
xdotool key --window "$HW" Return; sleep 1.5   # confirm
ok=
for _ in $(seq 1 20); do
  grep -aq "banning seat 2" "$OUT/host.log" && { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "HOST NEVER BANNED SEAT 2" host
grep -aq "token yes" "$OUT/host.log" ||
  room_fail "BAN RECORDED NO ACCOUNT TOKEN" host
# The banned process stays up on its REMOVED card (room_alive checks every
# launched pid); the rename below is a FRESH process, as a real evader is.

echo "== the same account under a NEW name is refused (the rename bypass)"
before=$(newtonia_windows)
ROOM_PIDS[2]=$(launch rename NEWTONIA_NET_NAME=MALLORY2 NEWTONIA_NET_TEST_CRED=ACCT1)
sleep 4
W=$(new_window_since "$before")
[ -n "$W" ] || room_fail "NO WINDOW FOR THE RENAME" rename
nav_join "$W" "$ROOM_CODE" rename
ok=
for _ in $(seq 1 40); do
  grep -aq "host refused this connection (reason 4)" "$OUT/rename.log" &&
    { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "RENAMED ACCOUNT WAS NOT REFUSED" rename
grep -aq "seat 2 filled" <(sed -n '/banning seat 2/,$p' "$OUT/host.log") &&
  room_fail "RENAMED ACCOUNT GOT A SEAT" host

echo "== a different account still seats (the ban took the account, not the room)"
before=$(newtonia_windows)
ROOM_PIDS[3]=$(launch clean NEWTONIA_NET_NAME=CLEAN NEWTONIA_NET_TEST_CRED=ACCT2)
sleep 4
W=$(new_window_since "$before")
[ -n "$W" ] || room_fail "NO WINDOW FOR CLEAN" clean
nav_join "$W" "$ROOM_CODE" clean
ok=
for _ in $(seq 1 40); do
  grep -aq "seat 2 filled" <(sed -n '/banning seat 2/,$p' "$OUT/host.log") &&
    { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "CLEAN ACCOUNT WAS NOT SEATED" host

room_alive
room_kill_all
echo "NSEAT-BAN-TOKEN-OK (token banned, rename refused, other account seated)"
