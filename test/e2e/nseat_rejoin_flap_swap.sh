#!/bin/bash
# O4's seat-re-map case — the half nseat_rejoin_flap.sh deliberately leaves
# out. That driver parks ONE seat, so the adoption in flight is always
# heading for the seat the door offered and "same seat" and "same pilot"
# answer alike. The interesting shape needs TWO parked seats:
#
#   seats 3 and 4 both parked; the door offers seat 3 (lowest parked); seat
#   4's PILOT answers it — nseat_swap's shape, where the WELCOME re-maps the
#   session onto seat 4 (#447) — and then seat 3's own pilot knocks while
#   that handshake is still in flight.
#
# A resolver that asked "does this joiner resolve to the seat the adoption
# is on?" would say yes and tear down a perfectly healthy exchange
# belonging to somebody else. net_host_rejoin_flap_check compares PILOTS
# instead, and this driver is what would notice if that ever changed back.
#
# Reproducing it needs the held handshake to STALL rather than die:
# TEST_FLAP's corpse would exercise the same comparison, but the assertion
# that gives this test its teeth is that the protected handshake goes on to
# COMPLETE — at seat 4, off its own claim — which a corpse can never show.
# Hence NEWTONIA_NET_TEST_HANG_MS (net_lobby.cpp), which holds ICE both
# ways for long enough to clear the resolver's liveness gate
# (FLAP_MIN_ADOPT_MS, 12 s) and then lets the connection finish.
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
HOLD_MS=22000

fly_all 4
# Seat 3 a beat BEFORE seat 4, so its RX watchdog trips first and the door
# deterministically arms for seat 3 — the same stagger nseat_swap needs,
# and here the door's seat is the thing the assertion is about.
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
# the door serves one seat at a time. The re-map below is its proof.
echo "seats 3+4 parked, door offering seat 3"

# Both instances get their windows up BEFORE the hold starts: from the
# moment the holder answers, everything below is racing a 22 s clock, and a
# process launch plus its window wait is ~5 s of that spent for nothing.
BEFORE=$(newtonia_windows)
PH=$(launch holder NEWTONIA_NET_NAME=PILOT3 NEWTONIA_NET_TEST_HANG_MS=$HOLD_MS)
sleep 4
WH=$(new_window_since "$BEFORE")
[ -n "$WH" ] || { echo "NO HOLDER WINDOW"; kill "$PH" 2>/dev/null; room_kill_all; exit 1; }
BEFORE=$(newtonia_windows)
PK=$(launch knocker NEWTONIA_NET_NAME=PILOT2)
sleep 4
WK=$(new_window_since "$BEFORE")
[ -n "$WK" ] || { echo "NO KNOCKER WINDOW"; kill "$PK" "$PH" 2>/dev/null; room_kill_all; exit 1; }

# Sampled before anyone joins: a re-arm from here on means the adoption
# died of its own accord and there is no longer a handshake to protect —
# the same lost race nseat_rejoin_flap.sh skips on rather than reddening.
REARMS=$(grep -ac "reopened for rejoin" "$OUT/host.log")

# --- seat 4's pilot answers seat 3's offer, then stalls -------------------
nav_join "$WH" "$ROOM_CODE" holder
HELD=
for _ in $(seq 1 30); do
  grep -aq "TEST_HANG: holding ICE" "$OUT/holder.log" && { HELD=1; break; }
  sleep 1
done
[ -n "$HELD" ] || room_fail "HOLDER NEVER SENT ITS ANSWER" holder
echo "PILOT3's answer is on the wire and its ICE is held"

# --- seat 3's own pilot knocks while that handshake is in flight ----------
nav_join "$WK" "$ROOM_CODE" knocker
SEEN=
for _ in $(seq 1 25); do
  grep -aq "while seat 3 is mid-handshake" "$OUT/host.log" && { SEEN=1; break; }
  sleep 1
done
[ -n "$SEEN" ] || room_fail "HOST NEVER FILED THE KNOCKER'S JOIN" host
echo "host filed PILOT2's join against seat 3's adoption"

# The verdict. Waiting on the host SAYING it declined, not on a clock: the
# absence of the drop line is equally true of a resolver that never ran,
# and the run cannot tell "12 s of liveness gate" from "the record was
# thrown away" by staring at a sleep.
#
# The DROP is tested first and every pass, ahead of the skip below, because
# the two are indistinguishable by their after-effects: a drop re-offers,
# so it re-arms the door exactly as a self-expiring adoption does. Checking
# the skip first made this driver exit 0 against a control build that did
# tear the handshake down — it swallowed the one regression it exists to
# catch (measured, on a seat-comparing build).
VERDICT=
for _ in $(seq 1 25); do
  grep -aq "$DROP_LINE" "$OUT/host.log" &&
    room_fail "SEAT 3'S PILOT TORE DOWN SEAT 4'S HANDSHAKE (seat-vs-pilot regression)" host
  grep -aq "who holds the handshake - leaving it alone" "$OUT/host.log" &&
    { VERDICT=1; break; }
  if [ "$(grep -ac "reopened for rejoin" "$OUT/host.log")" -gt "$REARMS" ]; then
    echo "SKIP: the held adoption died before the resolver's liveness gate"
    echo "      opened (12 s), so there was nothing left to protect. Re-run."
    kill -9 "$PK" "$PH" 2>/dev/null; room_kill_all
    exit 0
  fi
  sleep 1
done
[ -n "$VERDICT" ] || room_fail "RESOLVER NEVER RULED ON THE KNOCKER" host
echo "resolver left the handshake alone"

# Out of the room before the hold lifts. Once seat 4 seats, the door
# re-arms for seat 3 and its offer is UNADDRESSED — the knocker, still the
# oldest connected joiner, would eat the offer meant for the instance
# launched below (the squatter case nseat_rejoin_flap.sh names).
kill -9 "$PK" 2>/dev/null; wait "$PK" 2>/dev/null

# --- the hold lifts: the protected handshake must complete, at seat 4 -----
# This is what a corpse could not have shown. The exchange the resolver
# declined to touch was a real one, and it lands on the seat the WELCOME
# re-maps it to rather than the seat the door offered.
REMAPPED=
for _ in $(seq 1 45); do
  grep -aq "player 4 rejoined" "$OUT/host.log" && { REMAPPED=1; break; }
  sleep 1
done
[ -n "$REMAPPED" ] || room_fail "THE HELD HANDSHAKE NEVER COMPLETED" host
ROOM_PIDS[3]=$PH; ROOM_WINS[3]=$WH
grep -aq "rejoin identity-matched seat 4 (door was 3)" "$OUT/host.log" ||
  room_fail "PILOT3 WAS SEATED BY DOOR ORDER - NO RE-MAP, TEST PROVES NOTHING" host
grep -aq "$DROP_LINE" "$OUT/host.log" &&
  room_fail "THE ADOPTION WAS DROPPED AFTER ALL" host
echo "PILOT3 landed back on seat 4 through the re-map, handshake intact"

# --- and the room recovers: seat 3's pilot takes its own hull -------------
BEFORE=$(newtonia_windows)
PR=$(launch rejoin3 NEWTONIA_NET_NAME=PILOT2)
sleep 4
WR=$(new_window_since "$BEFORE")
[ -n "$WR" ] || { echo "NO REJOIN3 WINDOW"; kill "$PR" 2>/dev/null; room_kill_all; exit 1; }
nav_join "$WR" "$ROOM_CODE" rejoin3
BACK=
for _ in $(seq 1 45); do
  grep -aq "player 3 rejoined" "$OUT/host.log" && { BACK=1; break; }
  sleep 1
done
ROOM_PIDS[2]=$PR; ROOM_WINS[2]=$WR
[ -n "$BACK" ] || room_fail "SEAT 3'S PILOT NEVER REJOINED" host
grep -aq "bootstrap adopted" "$OUT/holder.log" ||
  room_fail "HOLDER NO BOOTSTRAP" holder
grep -aq "bootstrap adopted" "$OUT/rejoin3.log" ||
  room_fail "REJOIN3 NO BOOTSTRAP" rejoin3
echo "PILOT2 landed back on seat 3, roster whole"

sleep 3
room_alive
for i in "${!ROOM_WINS[@]}"; do
  shot "${ROOM_WINS[$i]}" "nseatflapswap-$(room_name "$i")"
done

room_kill_all
# The knocker's log is exempt: it is SIGKILLed with a join half-made by
# design, which is exactly the shape assert_clean reads as a failure
# everywhere else (same exemption nseat_rejoin_flap.sh gives its flapper).
assert_clean "$OUT/host.log" "$OUT/joiner1.log" "$OUT/holder.log" \
             "$OUT/rejoin3.log"
echo "NSEAT-REJOIN-FLAP-SWAP-OK"
