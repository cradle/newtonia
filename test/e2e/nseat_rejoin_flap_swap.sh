#!/bin/bash
# O4's seat-re-map case — the half nseat_rejoin_flap.sh cannot express.
# That driver parks ONE seat, so the adoption in flight is always heading
# for the seat the door offered: "same seat" and "same pilot" answer alike
# there, and no assertion in it can tell the resolver we have from the one
# FOURPLAYER.md O4 rejected. The interesting shape needs TWO parked seats:
#
#   seats 3 and 4 both parked; the door offers seat 3 (lowest parked); seat
#   4's PILOT answers it — nseat_swap's shape, where the WELCOME re-maps the
#   session onto seat 4 (#447) — and then seat 3's own pilot knocks while
#   that handshake is still in flight.
#
# A resolver asking "does this joiner resolve to the seat the adoption is
# on?" says yes and tears down an exchange belonging to somebody else.
# net_host_rejoin_flap_check compares PILOTS instead, and this driver is
# what notices if that ever changes back: it is RED on a build whose
# comparison is net_rejoin_seat_for_identity(who) == hs->seat.
#
# What it does NOT show, and the honest reason. The adoption here is a
# CORPSE (NEWTONIA_NET_TEST_FLAP — seat 4's pilot answers and dies with the
# answer on the wire), not a handshake still negotiating, so this proves the
# resolver leaves ANOTHER PILOT'S adoption alone without proving that one
# still completes afterwards. A hang hook was built for exactly that and
# thrown away: it held ICE on the trickle path, and with no TURN servers the
# candidates ride inline in the SDP instead, so the held handshake connected
# in 212 ms on CI while the hook logged that it was holding. Stalling ICE
# honestly would mean stripping candidate lines out of the SDP the game
# sends — test-only surgery on shipping netcode — and the comparison under
# test cannot tell a corpse from a stall anyway: both are !connected() past
# the liveness gate, which is all net_host_rejoin_flap_check looks at.
#
# The knocker is a scripted relay client (fake_joiner.mjs), not a game: the
# resolver consumes exactly two worker events from that socket — the
# PeerJoin and the identity frame naming its jid — and a real instance
# delivers them only after booting, showing a window and being walked
# through the menu, ~34 s of it on a loaded CI runner. It also cannot answer
# the offer, so it cannot squat the door.
#
# Prints "NSEAT-REJOIN-FLAP-SWAP-OK". Needs a local relay (see lib.sh).
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

SEATS=4
export NEWTONIA_NET_TEST_SEATS=$SEATS
# joiner I sits on seat I+1 and flies as PILOT<I> — seat 3 is PILOT2 and
# seat 4 is PILOT3. Distinct names are the whole input to the comparison
# under test.
room_joiner_env() { echo "NEWTONIA_NET_NAME=PILOT$1"; }
room_setup "$SEATS"

DROP_LINE="dropping the stale adoption"

fly_all 4
# Seat 3 a beat BEFORE seat 4, so its RX watchdog trips first and the door
# deterministically arms for seat 3 — the same stagger nseat_swap needs, and
# here the door's seat is the thing the assertion is about.
echo "== SIGKILL seat 3 (PILOT2), then seat 4 (PILOT3) a second later"
kill -9 "${ROOM_PIDS[2]}"
sleep 1
kill -9 "${ROOM_PIDS[3]}"
wait "${ROOM_PIDS[2]}" "${ROOM_PIDS[3]}" 2>/dev/null
ROOM_PIDS[2]=; ROOM_WINS[2]=; ROOM_PIDS[3]=; ROOM_WINS[3]=

LOST=
for _ in $(seq 1 45); do
  grep -aq "player 3 lost" "$OUT/host.log" && { LOST=1; break; }
  sleep 1
done
[ -n "$LOST" ] || room_fail "SEAT 3 LOSS NEVER DETECTED" host
# Seat 4's park is real but silent — "player N lost" is DOOR-ARM scoped and
# the door serves one seat at a time. The re-map at the end is its proof.
echo "seats 3+4 parked, door offering seat 3"

# Sampled before anyone joins: this counts door re-arms, and the corpse's
# own ICE timeout produces one. Everything below has to happen inside that.
REARMS=$(grep -ac "reopened for rejoin" "$OUT/host.log")

# --- seat 4's pilot answers seat 3's offer and dies on it -----------------
BEFORE=$(newtonia_windows)
PF=$(launch flapper NEWTONIA_NET_NAME=PILOT3 NEWTONIA_NET_TEST_FLAP=1)
sleep 4
WF=$(new_window_since "$BEFORE")
[ -n "$WF" ] || { echo "NO FLAPPER WINDOW"; kill "$PF" 2>/dev/null; room_kill_all; exit 1; }
nav_join "$WF" "$ROOM_CODE" flapper
FLAPPED=
for _ in $(seq 1 30); do
  grep -aq "TEST_FLAP: dying" "$OUT/flapper.log" && { FLAPPED=1; break; }
  sleep 1
done
[ -n "$FLAPPED" ] || room_fail "FLAPPER NEVER SENT ITS ANSWER" flapper
wait "$PF" 2>/dev/null
# Nothing logs the adoption forming, so give the host a beat to build it
# from the answer it just took. The knocker's record below is the proof it
# exists: the host only files one while an adoption is in flight.
sleep 3
room_alive
echo "PILOT3 died with its answer on the wire — seat 3 holds its adoption"

# --- seat 3's own pilot knocks while that adoption is in flight -----------
# Created here, not by the redirect: that runs in the forked child, so the
# first poll below can beat it and grep complains about a missing file.
: > "$OUT/knocker.log"
node "$(dirname "$0")/fake_joiner.mjs" "$ROOM_CODE" PILOT2 \
  >> "$OUT/knocker.log" 2>&1 &
PK=$!
JOINED=
for _ in $(seq 1 20); do
  grep -aq "^JOINED" "$OUT/knocker.log" && { JOINED=1; break; }
  grep -aq "^FAILED\|^CLOSED" "$OUT/knocker.log" &&
    room_fail "KNOCKER COULD NOT JOIN THE ROOM" knocker
  sleep 1
done
[ -n "$JOINED" ] || room_fail "KNOCKER NEVER JOINED THE ROOM" knocker

# Three CI failures were reported as "the knocker never filed" when the real
# story was upstream of it — no adoption left to file against. room_fail
# tails host.log only, so the evidence that would have said so was never in
# the output. Dump both sides on any failure from here on.
hold_diag() {
  echo "---- flapper ----"
  grep -a "TEST_FLAP\|answer sent\|joining room\|ice path\|connect failed" \
    "$OUT/flapper.log" | tail -8
  echo "---- knocker ----"; cat "$OUT/knocker.log"
  echo "---- host (door + adoption) ----"
  grep -a "reopened for rejoin\|mid-handshake\|rejoin answer\|unanswered\|rejoined\|leaving it alone\|dropping the stale" \
    "$OUT/host.log" | tail -12
  echo "----"
}
SEEN=
for _ in $(seq 1 20); do
  grep -aq "while seat 3 is mid-handshake" "$OUT/host.log" && { SEEN=1; break; }
  sleep 1
done
[ -n "$SEEN" ] || { hold_diag; room_fail "HOST NEVER FILED THE KNOCKER'S JOIN" host; }
echo "host filed PILOT2's join against seat 3's adoption"

# The verdict. Waiting on the host SAYING it declined, not on a clock: the
# absence of the drop line is equally true of a resolver that never ran, and
# the run cannot tell "12 s of liveness gate" from "the record was thrown
# away" by staring at a sleep.
#
# The DROP is tested first and every pass, ahead of the skip below, because
# the two are indistinguishable by their after-effects: a drop re-offers, so
# it re-arms the door exactly as a self-expiring corpse does. Checking the
# skip first made this driver exit 0 against a control build that did tear
# the adoption down — it swallowed the one regression it exists to catch.
VERDICT=
for _ in $(seq 1 30); do
  grep -aq "$DROP_LINE" "$OUT/host.log" &&
    room_fail "SEAT 3'S PILOT TORE DOWN SEAT 4'S ADOPTION (seat-vs-pilot regression)" host
  grep -aq "who holds the handshake - leaving it alone" "$OUT/host.log" &&
    { VERDICT=1; break; }
  if [ "$(grep -ac "reopened for rejoin" "$OUT/host.log")" -gt "$REARMS" ]; then
    echo "SKIP: the corpse expired before the resolver's liveness gate opened"
    echo "      (12 s), so there was nothing left to protect. Re-run."
    kill "$PK" 2>/dev/null; room_kill_all
    exit 0
  fi
  sleep 1
done
[ -n "$VERDICT" ] || { hold_diag; room_fail "RESOLVER NEVER RULED ON THE KNOCKER" host; }
echo "resolver left seat 4's pilot alone"

# Out of the room before the door re-arms. It cannot ANSWER the door, but it
# is still the oldest connected joiner, and the re-armed offer is
# unaddressed — leaving it there would hand seat 3's offer to a socket that
# will never use it (the squatter case nseat_rejoin_flap.sh names).
kill "$PK" 2>/dev/null; wait "$PK" 2>/dev/null

# --- and the room recovers, the slow way ----------------------------------
# The corpse now has to die of its own ICE timeout — the door serves one
# parked seat at a time and nothing will clear it early, because declining
# to is exactly what was asserted above. That wait IS the residual O4
# documents: a flapped rejoin the host cannot match to the returning pilot
# still costs the room the timeout. Waiting for the re-arm before launching
# is not politeness — a joiner that answers a busy door has its answer
# ignored and will not answer again.
REOPENED=
for _ in $(seq 1 90); do
  [ "$(grep -ac "reopened for rejoin" "$OUT/host.log")" -gt "$REARMS" ] &&
    { REOPENED=1; break; }
  sleep 1
done
[ -n "$REOPENED" ] || { hold_diag; room_fail "CORPSE NEVER EXPIRED - DOOR STAYED SHUT" host; }
echo "corpse expired, door re-armed for seat 3"

# Seat 4's pilot returns for real. The door is offering seat 3, so this also
# re-runs the re-map the whole scenario is built on.
BEFORE=$(newtonia_windows)
P4=$(launch rejoin4 NEWTONIA_NET_NAME=PILOT3)
sleep 4
W4=$(new_window_since "$BEFORE")
[ -n "$W4" ] || { echo "NO REJOIN4 WINDOW"; kill "$P4" 2>/dev/null; room_kill_all; exit 1; }
nav_join "$W4" "$ROOM_CODE" rejoin4
BACK=
for _ in $(seq 1 60); do
  grep -aq "player 4 rejoined" "$OUT/host.log" && { BACK=1; break; }
  sleep 1
done
ROOM_PIDS[3]=$P4; ROOM_WINS[3]=$W4
[ -n "$BACK" ] || { hold_diag; room_fail "SEAT 4'S PILOT NEVER REJOINED" host; }
grep -aq "rejoin identity-matched seat 4 (door was 3)" "$OUT/host.log" ||
  room_fail "PILOT3 WAS SEATED BY DOOR ORDER - NO RE-MAP" host
grep -aq "$DROP_LINE" "$OUT/host.log" &&
  room_fail "THE ADOPTION WAS DROPPED AFTER ALL" host
grep -aq "bootstrap adopted" "$OUT/rejoin4.log" ||
  room_fail "REJOIN4 NO BOOTSTRAP" rejoin4
echo "PILOT3 landed back on seat 4 through the re-map"

sleep 3
room_alive
for i in "${!ROOM_WINS[@]}"; do
  # Seat 3's slot stays cleared — its pilot never comes back here — and an
  # empty window id reaches xdotool as a BadWindow X error.
  [ -n "${ROOM_WINS[$i]}" ] || continue
  shot "${ROOM_WINS[$i]}" "nseatflapswap-$(room_name "$i")"
done

room_kill_all
# The flapper's log is exempt for the reason nseat_rejoin_flap.sh gives its
# own: it exits mid-handshake by design, and the half-built session logs a
# teardown that assert_clean reads as a failure everywhere else. The
# knocker's is not a game log at all (node, one word long).
assert_clean "$OUT/host.log" "$OUT/joiner1.log" "$OUT/rejoin4.log"
echo "NSEAT-REJOIN-FLAP-SWAP-OK"
