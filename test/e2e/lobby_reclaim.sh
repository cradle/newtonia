#!/bin/bash
# Waiting-room host reconnect (TODO.md "Waiting-room host: reconnect when the
# signal connection drops"). The relay closed an idle waiting-room host's
# socket ~35 min in on the kit (2026-10-27); the host ignored it, the worker
# deleted the hostless room after its 2 min grace, and the code on screen
# went dead. The host now reclaims the room with its token.
#
#   A. A 4-seat waiting room drops its relay socket (test hook) and holds the
#      reclaim back 15 s. A joiner knocks INSIDE that gap — the worker has no
#      host to tell — and must still be seated once the reclaim lands (the
#      worker names it to the reclaimed host as "waiting"). A second joiner
#      after the reclaim proves the room works as before.
#   B. The reclaim is refused (a wrong token, as a room past its grace would
#      be): an empty room says the room was lost instead of showing a dead
#      code.
# Prints LOBBY-RECLAIM-E2E-OK on success.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
cd "$(dirname "$0")"
. ./lib.sh

relay_check
export NEWTONIA_NET_TEST_SEATS=4

wait_log() {  # wait_log LOG PATTERN SECS
  local i
  for i in $(seq 1 "$3"); do
    grep -aq "$2" "$OUT/$1.log" && return 0
    sleep 1
  done
  return 1
}

echo "== A: joiner knocks while the host's socket is down"
ROOM_PIDS=(); ROOM_WINS=(); ROOM_CODE=
ROOM_PIDS[0]=$(launch host NEWTONIA_NET_TEST_LOBBY_DROP_SIGNAL_MS=3000 \
                           NEWTONIA_NET_TEST_LOBBY_RECLAIM_MS=15000)
sleep 2
ROOM_WINS[0]=$(newtonia_windows | head -1)
[ -n "${ROOM_WINS[0]}" ] || room_fail "NO HOST WINDOW" host
before=$(newtonia_windows)
ROOM_PIDS[1]=$(launch joiner1)
sleep 4
ROOM_WINS[1]=$(new_window_since "$before")
[ -n "${ROOM_WINS[1]}" ] || room_fail "NO WINDOW FOR joiner1" joiner1

nav_host "${ROOM_WINS[0]}"
ROOM_CODE=$(host_room_code host)
[ -n "$ROOM_CODE" ] || room_fail "NO ROOM CODE" host
echo "room code: $ROOM_CODE"
wait_log host "TEST dropping the relay socket" 20 ||
  room_fail "HOST NEVER DROPPED ITS SOCKET" host

nav_join "${ROOM_WINS[1]}" "$ROOM_CODE" joiner1
wait_log joiner1 "ROOM FOUND\|joining room" 10 || true
# The join must land INSIDE the gap for this to test anything.
grep -aq "reclaiming room" "$OUT/host.log" &&
  room_fail "JOINER WAS TOO SLOW: the reclaim beat it (not a test)" host

wait_log host "room $ROOM_CODE reclaimed" 30 ||
  room_fail "HOST NEVER RECLAIMED THE ROOM" host
wait_log host "was waiting during the reclaim" 10 ||
  room_fail "RECLAIMED HOST WAS NOT TOLD ABOUT THE WAITING JOINER" host
wait_log host "seat 2 filled" 40 ||
  room_fail "GAP JOINER NEVER SEATED" host
echo "gap joiner seated"
room_alive

before=$(newtonia_windows)
ROOM_PIDS[2]=$(launch joiner2)
sleep 4
ROOM_WINS[2]=$(new_window_since "$before")
[ -n "${ROOM_WINS[2]}" ] || room_fail "NO WINDOW FOR joiner2" joiner2
nav_join "${ROOM_WINS[2]}" "$ROOM_CODE" joiner2
wait_log host "seat 3 filled" 40 ||
  room_fail "POST-RECLAIM JOINER NEVER SEATED" host
echo "post-reclaim joiner seated"
room_alive
shot "${ROOM_WINS[0]}" reclaim-host
room_kill_all
assert_clean "$OUT/host.log" "$OUT/joiner1.log" "$OUT/joiner2.log"
mv "$OUT/host.log" "$OUT/host-a.log"

echo "== B: the reclaim is refused"
fresh_menu_state 2>/dev/null || true
ROOM_PIDS=(); ROOM_WINS=()
ROOM_PIDS[0]=$(launch host NEWTONIA_NET_TEST_LOBBY_DROP_SIGNAL_MS=2000 \
                           NEWTONIA_NET_TEST_LOBBY_RECLAIM_MS=500 \
                           NEWTONIA_NET_TEST_LOBBY_RECLAIM_BAD_TOKEN=1)
wait_for_menu host || room_fail "HOST NEVER REACHED THE MENU" host
ROOM_WINS[0]=$(newtonia_windows | head -1)
nav_host "${ROOM_WINS[0]}"
CODE_B=$(host_room_code host)
[ -n "$CODE_B" ] || room_fail "NO ROOM CODE (B)" host
wait_log host "room $CODE_B lost (no-such-room)" 30 ||
  room_fail "REFUSED RECLAIM NOT REPORTED" host
room_alive
shot "${ROOM_WINS[0]}" reclaim-lost
room_kill_all
assert_clean "$OUT/host.log"
echo "LOBBY-RECLAIM-E2E-OK"
