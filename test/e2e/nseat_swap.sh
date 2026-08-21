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
# (joiner3) — seat 3 a beat BEFORE seat 4, so its RX watchdog (10 s
# after each seat's own last INPUT) deterministically trips first and
# the door arms for seat 3. A same-instant kill left the trip order to
# input-arrival jitter: seat 4 tripping milliseconds earlier armed the
# door for seat 4, "player 3 lost" (a DOOR-ARM-scoped line) never
# logged, and the wait below timed out — a flake, not a product bug
# (the identity resolver doesn't care which seat the door serves). The
# swap shape this test exists for — BOTH seats parked, pilots returning
# in the other order — is unchanged by the stagger.
fly_all 4
echo "== SIGKILL seat 3, then seat 4 a second later"
kill -9 "${ROOM_PIDS[2]}"
sleep 1
kill -9 "${ROOM_PIDS[3]}"
wait "${ROOM_PIDS[2]}" "${ROOM_PIDS[3]}" 2>/dev/null
ROOM_PIDS[2]=; ROOM_WINS[2]=; ROOM_PIDS[3]=; ROOM_WINS[3]=

# The "player N lost" line is DOOR-ARM scoped and the door serves one
# seat at a time (lowest parked first): with both seats parked only
# "player 3 lost" logs — seat 4's park is real but silent until the door
# re-arms for it. The identity-matched rejoin below is the proof seat 4
# was parked.
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
nav_join "$W4" "$ROOM_CODE" rejoin4
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
nav_join "$W3" "$ROOM_CODE" rejoin3
ok=
for _ in $(seq 1 45); do
  grep -aq "player 3 rejoined" "$OUT/host.log" && { ok=1; break; }
  sleep 1
done
ROOM_PIDS[2]=$P3; ROOM_WINS[2]=$W3
[ -n "$ok" ] || room_fail "SEAT 3'S PILOT NEVER REJOINED" host
# Poll, never one-shot: the host's "rejoined" marks adoption and each
# client's bootstrap lands a beat later (next 10 Hz keyframe slot + chunk +
# apply — see nseat_rejoin_flap_swap.sh, which lost this race on CI).
BOOT=
for _ in $(seq 1 30); do
  grep -aq "bootstrap adopted" "$OUT/rejoin4.log" &&
    grep -aq "bootstrap adopted" "$OUT/rejoin3.log" && { BOOT=1; break; }
  sleep 1
done
[ -n "$BOOT" ] || {
  grep -aq "bootstrap adopted" "$OUT/rejoin4.log" ||
    room_fail "REJOIN4 NO BOOTSTRAP" rejoin4
  room_fail "REJOIN3 NO BOOTSTRAP" rejoin3
}
echo "pilot PILOT2 landed back on seat 3"

sleep 3
room_alive
room_kill_all
assert_clean "$OUT"/*.log
echo "NSEAT-SWAP-OK"
