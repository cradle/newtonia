#!/bin/bash
# O4 — a rejoin that half-establishes and flaps must not cost the room the
# ~30 s ICE timeout. At 4 seats with NAMED pilots, seat 3's client is
# SIGKILLed, and its pilot's FIRST return dies with its answer already on
# the wire (NEWTONIA_NET_TEST_FLAP): the host builds an adoption session
# from that answer and is left holding a corpse, which blocks the door for
# every parked seat.
#
# Two assertions, and the second is the one that could regress:
#   POSITIVE — the same pilot's next attempt is recognised by identity, so
#     the host drops the stale adoption and re-offers instead of waiting
#     the transport out.
#   NEGATIVE — a DIFFERENT pilot joining while that handshake is live must
#     leave it alone. Tearing down an in-flight exchange on the mere fact
#     of a join is the "AGES to reconnect" bug the N=1 branch documents,
#     and it is exactly what this fix would do if it stopped checking who
#     joined.
#
# NOT covered here, deliberately: the case where the in-flight handshake
# belongs to a pilot heading for a DIFFERENT seat than the door offered
# (two seats parked, the higher seat's pilot answering the lower seat's
# offer — nseat_swap's shape). That is why the resolver compares pilots
# rather than seats; comparing seats would tear that handshake down when
# its own seat's pilot arrived. Reproducing it needs a handshake that
# HANGS rather than dies — TEST_FLAP kills the socket, and a corpse is
# not the case at issue — so it would take a second hook whose only user
# is this one assertion. The guarantee rests on the comparison being
# seat-independent by construction, not on a driver.
#
# Prints "NSEAT-REJOIN-FLAP-OK". Needs a local relay (see lib.sh).
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

SEATS=4
export NEWTONIA_NET_TEST_SEATS=$SEATS
# joiner I sits on seat I+1 and flies as PILOT<I> — seat 3 is PILOT2, and
# distinct names are what the resolver matches on.
room_joiner_env() { echo "NEWTONIA_NET_NAME=PILOT$1"; }
room_setup "$SEATS"

DROP_LINE="dropping the stale adoption"

fly_all 4
echo "== SIGKILL seat 3's client (joiner2 / PILOT2)"
kill -9 "${ROOM_PIDS[2]}"; wait "${ROOM_PIDS[2]}" 2>/dev/null
ROOM_PIDS[2]=; ROOM_WINS[2]=

LOST=
for _ in $(seq 1 45); do
  grep -aq "player 3 lost" "$OUT/host.log" && { LOST=1; break; }
  sleep 1
done
[ -n "$LOST" ] || room_fail "SEAT 3 LOSS NEVER DETECTED" host
echo "seat 3 lost, door open"

# --- the flap: answer, then die -------------------------------------------
BEFORE=$(newtonia_windows)
PF=$(launch flapper NEWTONIA_NET_NAME=PILOT2 NEWTONIA_NET_TEST_FLAP=1)
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
# The host needs the answer it just received to become an adoption before
# the checks below mean anything; nothing logs that, so give the poll a
# beat. The POSITIVE assertion is what really proves the corpse existed —
# with no stale adoption there is nothing for the host to drop, and the
# drop line could not appear.
sleep 3
room_alive
echo "flapper died with its answer on the wire"

# Both remaining instances start NOW, before either joins. The corpse is
# racing its own ICE timeout from the moment it forms, and every second
# spent launching a process and waiting for its window is a second of that
# budget: with the rejoiner already sitting in the menu, the positive
# phase below costs a nav and nothing else. (`reopened for rejoin` count
# is the tell if the race is lost anyway — see the check below.)
BEFORE=$(newtonia_windows)
PS=$(launch stranger NEWTONIA_NET_NAME=PILOT9)
sleep 4
WS=$(new_window_since "$BEFORE")
[ -n "$WS" ] || { echo "NO STRANGER WINDOW"; kill "$PS" 2>/dev/null; room_kill_all; exit 1; }
BEFORE=$(newtonia_windows)
PR=$(launch rejoiner NEWTONIA_NET_NAME=PILOT2)
sleep 4
WR=$(new_window_since "$BEFORE")
[ -n "$WR" ] || { echo "NO REJOINER WINDOW"; kill "$PR" "$PS" 2>/dev/null; room_kill_all; exit 1; }
REARMS=$(grep -ac "reopened for rejoin" "$OUT/host.log")

# --- NEGATIVE: a stranger must not disturb the stale handshake ------------
# PILOT9 matches no parked seat, so the resolver returns "unknown" and the
# host must sit on its hands. Killed again straight after: an unaddressed
# rejoin offer goes to the oldest connected joiner, so a squatter left in
# the room could answer the door meant for PILOT2 and take seat 3.
nav_join "$WS" "$ROOM_CODE" stranger
sleep 6   # past NET_JOIN_IDENT_WAIT_MS (3 s) with room to spare
grep -aq "$DROP_LINE" "$OUT/host.log" &&
  room_fail "STRANGER TORE DOWN A LIVE HANDSHAKE (O4 regression)" host
echo "stranger ignored, handshake untouched"
kill -9 "$PS" 2>/dev/null; wait "$PS" 2>/dev/null

# --- POSITIVE: the seat's own pilot returns -------------------------------
# If the corpse died of its own ICE timeout while the negative phase ran,
# the door re-armed and there is no stale adoption left to drop: the
# positive assertion would then fail on perfectly good code. That is a lost
# race, not a product failure, and it says so.
if [ "$(grep -ac "reopened for rejoin" "$OUT/host.log")" -gt "$REARMS" ]; then
  room_fail "INCONCLUSIVE: the corpse expired before the rejoiner arrived (its ICE timeout beat the test) - re-run" host
fi
START=$(date +%s)
nav_join "$WR" "$ROOM_CODE" rejoiner
REJOINED=
for _ in $(seq 1 45); do
  grep -aq "player 3 rejoined" "$OUT/host.log" && { REJOINED=1; break; }
  sleep 1
done
ELAPSED=$(( $(date +%s) - START ))
ROOM_PIDS[2]=$PR; ROOM_WINS[2]=$WR   # back in the roster for teardown
[ -n "$REJOINED" ] || room_fail "SEAT 3 NEVER REJOINED AFTER THE FLAP" host

# The mechanism, not the clock, is the real assertion: this line means the
# host identified the returning pilot and cleared the corpse deliberately.
# The corpse could also die on its own — a local relay's ICE can give up
# well inside the ~30 s a real network takes — so a timing bound alone
# would pass on the unfixed build here and fail only in the field.
grep -aq "$DROP_LINE" "$OUT/host.log" ||
  room_fail "REJOINED WITHOUT DROPPING THE STALE ADOPTION (waited it out?)" host
[ "$ELAPSED" -lt 25 ] || room_fail "REJOIN TOOK ${ELAPSED}s (>= 25)" host
echo "seat 3 rejoined in ${ELAPSED}s via the identity drop"

sleep 3
room_alive
for i in "${!ROOM_WINS[@]}"; do
  shot "${ROOM_WINS[$i]}" "nseatflap-$(room_name "$i")"
done

room_kill_all
# The flapper's own log is exempt: it exits mid-handshake by design, and
# its half-built session logs the transport teardown that assert_clean
# reads as a failure everywhere else.
assert_clean "$OUT/host.log" "$OUT"/joiner*.log "$OUT/rejoiner.log"
echo "NSEAT-REJOIN-FLAP-OK"
