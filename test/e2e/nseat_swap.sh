#!/bin/bash
# Rejoin-by-identity (closing the B5 known limit): at 4 seats with NAMED
# pilots (NEWTONIA_NET_NAME per joiner), seats 3 AND 4 are SIGKILLed
# together, then their players rejoin in the OTHER order — seat 4's pilot
# first, while the door is serving seat 3 (lowest parked). The HELLO
# claim must re-map the WELCOME: the host logs the identity match and
# seats each pilot back on their OWN hull instead of swapping them.
# Prints "NSEAT-SWAP-OK". Needs a local relay (see lib.sh).
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

SEATS=4
export NEWTONIA_NET_TEST_SEATS=$SEATS
# joiner I sits on seat I+1 and flies as PILOT<I> — distinct names are
# what the identity door matches on.
room_joiner_env() { echo "NEWTONIA_NET_NAME=PILOT$1"; }
room_setup "$SEATS"

# INPUT flowing on every seat, then kill seats 3 (joiner2) and 4
# (joiner3) in the same instant — the simultaneous-drop shape that used
# to swap hulls.
fly_all 4
echo "== SIGKILL seats 3 and 4 together"
kill -9 "${ROOM_PIDS[2]}" "${ROOM_PIDS[3]}"
wait "${ROOM_PIDS[2]}" "${ROOM_PIDS[3]}" 2>/dev/null
ROOM_PIDS[2]=; ROOM_WINS[2]=; ROOM_PIDS[3]=; ROOM_WINS[3]=

# The "player N lost" line is DOOR-ARM scoped and the door serves one
# seat at a time (lowest parked first): with both seats parked only
# "player 3 lost" logs — seat 4's park is real but silent until the door
# re-arms for it. Waiting on seat 3's line covers both (the watchdog
# trips them within the same second), and the identity-matched rejoin
# below is the proof seat 4 was parked.
ok=
for _ in $(seq 1 45); do
  grep -aq "player 3 lost" "$OUT/host.log" && { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "SEAT 3 LOSS NEVER DETECTED" host
grep -aq "paused awaiting rejoin" "$OUT/host.log" &&
  room_fail "PAUSED WITH A LIVE PEER (play-on violated)" host
room_alive
echo "seats 3+4 lost, play continued"

# Seat 4's pilot returns FIRST. The door is offering seat 3 (lowest
# parked) — the resolver must re-map the WELCOME to seat 4 off the
# PILOT3 claim and the host must log both the match and the rejoin.
BEFORE=$(newtonia_windows)
P4=$(launch rejoin4 NEWTONIA_NET_NAME=PILOT3)
sleep 4
W4=$(new_window_since "$BEFORE")
[ -n "$W4" ] || { kill "$P4" 2>/dev/null; room_fail "NO REJOIN4 WINDOW"; }
nav_join "$W4" "$ROOM_CODE"
ok=
for _ in $(seq 1 45); do
  grep -aq "player 4 rejoined" "$OUT/host.log" && { ok=1; break; }
  sleep 1
done
ROOM_PIDS[3]=$P4; ROOM_WINS[3]=$W4
[ -n "$ok" ] || room_fail "SEAT 4'S PILOT NEVER REJOINED" host
grep -aq "rejoin identity-matched seat 4 (door was 3)" "$OUT/host.log" ||
  room_fail "NO IDENTITY MATCH - PILOT3 WAS SEATED BY DOOR ORDER" host
grep -aq "player 3 rejoined" "$OUT/host.log" &&
  room_fail "SEAT 3 REJOINED BY THE WRONG PILOT (hull swap)" host
echo "pilot PILOT3 landed back on seat 4"

# Then seat 3's own pilot: the re-armed door serves seat 3, the claim
# matches it (no re-map needed), and the roster is whole again.
BEFORE=$(newtonia_windows)
P3=$(launch rejoin3 NEWTONIA_NET_NAME=PILOT2)
sleep 4
W3=$(new_window_since "$BEFORE")
[ -n "$W3" ] || { kill "$P3" 2>/dev/null; room_fail "NO REJOIN3 WINDOW"; }
nav_join "$W3" "$ROOM_CODE"
ok=
for _ in $(seq 1 45); do
  grep -aq "player 3 rejoined" "$OUT/host.log" && { ok=1; break; }
  sleep 1
done
ROOM_PIDS[2]=$P3; ROOM_WINS[2]=$W3
[ -n "$ok" ] || room_fail "SEAT 3'S PILOT NEVER REJOINED" host
grep -aq "bootstrap adopted" "$OUT/rejoin4.log" ||
  room_fail "REJOIN4 NO BOOTSTRAP" rejoin4
grep -aq "bootstrap adopted" "$OUT/rejoin3.log" ||
  room_fail "REJOIN3 NO BOOTSTRAP" rejoin3
echo "pilot PILOT2 landed back on seat 3"

sleep 3
room_alive
room_kill_all
assert_clean "$OUT"/*.log
echo "NSEAT-SWAP-OK"
