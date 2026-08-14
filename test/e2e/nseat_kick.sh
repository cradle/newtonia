#!/bin/bash
# Host kick (FOURPLAYER.md O3), mid-game half: a 3-seat room, then the host
# opens the pause roster and BANS seat 3 (right on the row swaps the action
# from KICK, which they could come back from, to BAN, which they cannot —
# nseat_lobby_kick.sh covers the kick side).
#
# What it proves, in the order the failures would bite:
#   1. the kicked peer is TOLD (EV_KICKED) rather than just dropped — a bare
#      disconnect would send it back through the rejoin door and undo this;
#   2. its card is the kicked one, not "the host left" or "connection lost";
#   3. it does NOT rejoin (the whole point of the event);
#   4. the room plays on for the host and the OTHER peer — a kick must not
#      pause or end anyone else's game (the play-on policy, PB-D7).
#
# SEATS defaults to 3: two peers, so there is a bystander to check.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
cd "$(dirname "$0")"

# BAN is only OFFERED on a peer whose name the worker attested — a claimed
# name is a self-report they can change on their next handshake, so a ban
# keyed on one promises nothing (net_identity_bannable). The shared :8787
# dev relay attests nothing, so this driver self-hosts its own FAKE_VERIFY
# relay on a private port, exactly as identity_attested.sh does. Without
# it the roster would show KICK alone and the right-arrow would do nothing.
PORT="${KICK_RELAY_PORT:-8789}"
export NEWTONIA_SIGNAL_URL="ws://127.0.0.1:$PORT/ws"
( cd ../../signal &&
  exec npx wrangler@4 dev --local --port "$PORT" --var FAKE_VERIFY:1 ) \
  > /tmp/nseat_kick_wrangler.log 2>&1 &
WPID=$!
# kill_tree, not a bare kill: TERMing the npx wrapper leaves its node/
# workerd children orphaned and holding the port (lib.sh). The plain-kill
# fallback covers an exit before lib.sh is sourced (the relay-died path).
trap 'kill_tree $WPID 2>/dev/null || kill $WPID 2>/dev/null' EXIT
echo "== starting FAKE_VERIFY relay on :$PORT (pid $WPID)"
for _ in $(seq 1 90); do
  curl -s -o /dev/null "http://127.0.0.1:$PORT/" && break
  kill -0 $WPID 2>/dev/null || { echo "relay died:"; cat /tmp/nseat_kick_wrangler.log; exit 1; }
  sleep 1
done

. ./lib.sh

SEATS=${SEATS:-3}
[ "$SEATS" -ge 3 ] || { echo "SEATS must be >= 3 (need a bystander)"; exit 1; }
relay_check

# Named pilots: the ban is keyed on IDENTITY (a jid is per-socket), so the
# kicked pilot needs a name for the ban half of this test to mean anything.
room_joiner_env() { echo "NEWTONIA_NET_NAME=PILOT$1"; }

room_setup "$SEATS" NEWTONIA_NET_TEST_SEATS="$SEATS"

VICTIM=$((SEATS - 1))          # last joiner index -> seat $SEATS
VICTIM_SEAT=$SEATS
BYSTANDER=1                    # joiner1, seat 2

echo "== host: pause -> PLAYERS -> select seat $VICTIM_SEAT -> ban"
HW=${ROOM_WINS[0]}
xdotool key --window "$HW" p; sleep 1          # pause
xdotool key --window "$HW" s; sleep 0.4        # RESUME -> PLAYERS
xdotool key --window "$HW" Return; sleep 0.8   # open the roster
# Row 0 is the host's own seat; step down to the victim's row.
for _ in $(seq 1 $((VICTIM_SEAT - 1))); do
  xdotool key --window "$HW" s; sleep 0.3
done
# The host must have SEEN the attestation before BAN is on offer, or the
# right-arrow lands on a row still showing KICK alone.
ok=
for _ in $(seq 1 20); do
  grep -aq "identity attested (joiner.*name='PILOT$VICTIM'" "$OUT/host.log" && { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "HOST NEVER SAW THE VICTIM'S ATTESTATION" host
xdotool key --window "$HW" d; sleep 0.4        # KICK -> BAN
xdotool key --window "$HW" Return; sleep 0.5   # arm
xdotool key --window "$HW" Return; sleep 1.5   # confirm

ok=
for _ in $(seq 1 20); do
  grep -aq "banning player $VICTIM_SEAT" "$OUT/host.log" && { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "HOST NEVER BANNED SEAT $VICTIM_SEAT" host

echo "== the kicked peer must be told, not merely dropped"
ok=
for _ in $(seq 1 20); do
  grep -aq "kicked by the host" "$OUT/joiner$VICTIM.log" && { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "KICKED PEER NEVER GOT THE EVENT" "joiner$VICTIM"

echo "== and must not come back"
sleep 8
# Only what happened AFTER the kick counts. A kicked client goes quiet, so
# its ORIGINAL "bootstrap adopted" stays near the tail of the log and a
# plain tail|grep reports a rejoin that never happened (it did, first time
# this ran). Slice from the kick notice forward instead.
if sed -n '/kicked by the host/,$p' "$OUT/joiner$VICTIM.log" |
     grep -aq "bootstrap adopted"; then
  room_fail "KICKED PEER REJOINED" "joiner$VICTIM"
fi

echo "== the room plays on for everyone else"
# room_alive checks every still-tracked instance and tears the room down on
# a death, rather than leaving the survivors to die with the driver. The
# kicked instance is still tracked and still RUNNING — a kick ends its
# session, not its process (it sits on the removed-from-the-game card).
room_alive
grep -aq "kicked by the host" "$OUT/joiner$BYSTANDER.log" &&
  room_fail "BYSTANDER WAS KICKED TOO" "joiner$BYSTANDER"

echo "== and a FRESH instance with the kicked pilot's name is barred"
# The kicked client goes terminal, so it never retries by itself. This is
# the real "they come back" case: a new process, same pilot name, same
# room code. The ban is identity-keyed, so it must refuse this one while
# the room still has the seat open.
before=$(newtonia_windows)
BAN_PID=$(launch banned NEWTONIA_NET_NAME="PILOT$VICTIM")
sleep 4
BAN_WIN=$(new_window_since "$before")
[ -n "$BAN_WIN" ] || room_fail "NO WINDOW FOR THE BANNED RETRY" banned
nav_join "$BAN_WIN" "$ROOM_CODE" banned
ok=
for _ in $(seq 1 30); do
  grep -aq "refusing banned pilot" "$OUT/host.log" && { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "BANNED PILOT WAS NOT REFUSED" host
grep -aq "seat $VICTIM_SEAT filled" <(sed -n '/banning player/,$p' "$OUT/host.log") &&
  room_fail "BANNED PILOT GOT A SEAT" host
kill "$BAN_PID" 2>/dev/null

# ---- the plain KICK, on the bystander -------------------------------------
# Not a duplicate of the ban above: on THIS path nothing intercepts the
# kicked peer, and that is where the door bug lived. A kicked peer keeps its
# session for ~600 ms so the goodbye can reach the wire, which leaves it
# looking exactly like a rejoin in flight — lost, parked, holding a READY
# session. The door adopted it on the very next tick: unparking the hull just
# frozen and announcing "PLAYER N RECONNECTED" about the player just removed
# (field, 2026-08-13). The ban case hid this, because the door's banned-pilot
# check happened to drop the session first.
echo "== a plain KICK on the bystander"
# The ban above left the game PAUSED with the roster still open (removing a
# player doesn't close the screen — a host may want to remove two). So back
# out to the pause menu, which is still sitting on PLAYERS, and re-enter:
# that re-entry resets the roster highlight to row 0.
xdotool key --window "$HW" Escape; sleep 0.6   # roster -> pause menu
xdotool key --window "$HW" Return; sleep 0.8   # PLAYERS -> roster, row 0
xdotool key --window "$HW" s; sleep 0.4        # row 0 (host) -> seat 2
xdotool key --window "$HW" Return; sleep 0.5   # arm (KICK, no right press)
xdotool key --window "$HW" Return; sleep 1.5   # confirm
ok=
for _ in $(seq 1 20); do
  grep -aq "kicking player 2" "$OUT/host.log" && { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "HOST NEVER KICKED SEAT 2" host
ok=
for _ in $(seq 1 20); do
  grep -aq "kicked by the host" "$OUT/joiner$BYSTANDER.log" && { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "KICKED BYSTANDER NEVER GOT THE EVENT" "joiner$BYSTANDER"

echo "== and the host must not announce them as RECONNECTED"
sleep 4   # well past the goodbye drain, where the false adoption fired
awk '/kicking player 2/{f=1} f' "$OUT/host.log" | grep -aq "RECONNECTED" &&
  room_fail "HOST ANNOUNCED THE KICKED PEER AS RECONNECTED" host

room_kill_all
echo "NSEAT-KICK-OK (seats=$SEATS, banned seat $VICTIM_SEAT, kicked seat 2)"
