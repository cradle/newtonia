#!/bin/bash
# B6 N-seat drop + rejoin (SEATS=3|4, default 4): seat 3's client is
# SIGKILLed mid-game — at 4 seats that is a MIDDLE seat, so the roster
# goes non-contiguous (2,4) and the rejoin door must still serve seat 3,
# not an end slot. The host must mark player 3 lost and keep playing
# UNPAUSED (PB-D7 play-on: other remote peers are still in it), park only
# that hull, and seat a relaunched client back on seat 3. Prints
# "NSEAT-REJOIN-OK seats=N". Needs a local relay (see lib.sh).
set -u
SEATS="${SEATS:-4}"
[ "$SEATS" -ge 3 ] && [ "$SEATS" -le 4 ] || { echo "SEATS must be 3 or 4"; exit 1; }
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

export NEWTONIA_NET_TEST_SEATS=$SEATS
room_setup "$SEATS"

# Everyone plays a few seconds so seat 3 has INPUT flowing (the RX
# watchdog needs have_input), then kill seat 3's client outright.
fly_all 4
echo "== SIGKILL seat 3's client (joiner2)"
kill -9 "${ROOM_PIDS[2]}"; wait "${ROOM_PIDS[2]}" 2>/dev/null
ROOM_PIDS[2]=; ROOM_WINS[2]=

# Loss detect (ICE failure or the 10 s RX watchdog) -> park seat 3, doors
# reopen. PB-D7: play CONTINUES — the host must NOT auto-pause while
# another remote peer is live.
LOST=
for i in $(seq 1 45); do
  grep -aq "player 3 lost" "$OUT/host.log" && { LOST=1; break; }
  sleep 1
done
[ -n "$LOST" ] || room_fail "SEAT 3 LOSS NEVER DETECTED" host
grep -aq "paused awaiting rejoin" "$OUT/host.log" &&
  room_fail "PAUSED WITH A LIVE PEER (play-on violated)" host
room_alive
echo "seat 3 lost, play continued"

# Relaunch seat 3's player: a rejoin is a plain JOIN with the same code.
BEFORE=$(newtonia_windows)
PR=$(launch rejoiner)
sleep 4
WR=$(new_window_since "$BEFORE")
[ -n "$WR" ] || { echo "NO REJOINER WINDOW"; kill "$PR" 2>/dev/null; room_kill_all; exit 1; }
nav_join "$WR" "$ROOM_CODE" rejoiner
REJOINED=
for i in $(seq 1 45); do
  grep -aq "player 3 rejoined" "$OUT/host.log" && { REJOINED=1; break; }
  sleep 1
done
ROOM_PIDS[2]=$PR; ROOM_WINS[2]=$WR   # back in the roster for teardown
[ -n "$REJOINED" ] || room_fail "SEAT 3 NEVER REJOINED" host
# Poll, never one-shot: the host's "rejoined" marks adoption and the
# client's bootstrap lands a beat later (next 10 Hz keyframe slot + chunk +
# apply — see nseat_rejoin_flap_swap.sh, which lost this race on CI).
BOOT=
for _ in $(seq 1 30); do
  grep -aq "bootstrap adopted" "$OUT/rejoiner.log" && { BOOT=1; break; }
  sleep 1
done
[ -n "$BOOT" ] || room_fail "REJOINER NO BOOTSTRAP" rejoiner
echo "seat 3 rejoined"

sleep 3
room_alive
for i in "${!ROOM_WINS[@]}"; do
  shot "${ROOM_WINS[$i]}" "nseatrj$SEATS-$(room_name "$i")"
done

room_kill_all
assert_clean "$OUT"/*.log
echo "NSEAT-REJOIN-OK seats=$SEATS"
