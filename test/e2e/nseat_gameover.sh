#!/bin/bash
# B6 N-seat game over (SEATS=3|4, default 4): the host launches with the
# NEWTONIA_NET_TEST_KILL_MS/"all" hook (host-only env — lives are
# host-authoritative), which empties every seat's lives 20 s into play.
# Every CLIENT must then OBSERVE the game over through the replicated
# lives ("game over (all players out)" in tick_net_client) and sit on the
# shared GAME OVER card without crashing — including through the host's
# deliberate room teardown afterwards. Prints "NSEAT-GAMEOVER-OK seats=N".
# Needs a local relay (see lib.sh).
set -u
SEATS="${SEATS:-4}"
[ "$SEATS" -ge 3 ] && [ "$SEATS" -le 4 ] || { echo "SEATS must be 3 or 4"; exit 1; }
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

export NEWTONIA_NET_TEST_SEATS=$SEATS
room_setup "$SEATS" NEWTONIA_NET_TEST_KILL_MS=20000 NEWTONIA_NET_TEST_KILL_WHO=all

echo "== everyone plays until the kill hook fires"
fly_all 6
KILLED=
for i in $(seq 1 40); do
  grep -aq "TEST forcing everyone out of lives" "$OUT/host.log" && { KILLED=1; break; }
  sleep 1
done
[ -n "$KILLED" ] || room_fail "KILL HOOK NEVER FIRED" host

# Observation can trail the hook by 20+ s: a seat that was mid-respawn
# when its lives emptied only reads as out once its replicated wreck
# state settles, and the spread across 3 clients is real. Wait wide.
echo "== waiting for every client to observe the game over"
for i in $(seq 1 $((SEATS - 1))); do
  OVER=
  for j in $(seq 1 60); do
    grep -aq "game over (all players out)" "$OUT/joiner$i.log" && { OVER=1; break; }
    sleep 1
  done
  [ -n "$OVER" ] || room_fail "JOINER$i NEVER SAW GAME OVER" "joiner$i"
  echo "joiner$i saw game over"
done

# The card must hold: everyone alive on it.
sleep 3
room_alive
for i in "${!ROOM_WINS[@]}"; do
  shot "${ROOM_WINS[$i]}" "nseatgo$SEATS-$(room_name "$i")"
done

# Host leaves first — to the MENU (a keypress on the card after its 3 s
# grace): the ~GLGame teardown broadcasts the deliberate EV_BYE with the
# process still alive to flush it. (A SIGTERM'd host exits before
# libdatachannel flushes, so the BYE never arrives — measured at 2P and
# 4P alike; that path is loss-detection weather, not the goodbye.) After
# a game over the departure is the EXPECTED way out — every client must
# log the goodbye ("host bye - no rejoin"), stay on the card, and never
# treat it as a loss to recover from (no auto-rejoin, no crash).
echo "== host leaves to the menu after game over"
key "${ROOM_WINS[0]}" Escape
for i in $(seq 1 $((SEATS - 1))); do
  BYE=
  for j in $(seq 1 15); do
    grep -aq "host bye - no rejoin" "$OUT/joiner$i.log" && { BYE=1; break; }
    sleep 1
  done
  [ -n "$BYE" ] || room_fail "JOINER$i NEVER SAW THE HOST'S BYE" "joiner$i"
  echo "joiner$i saw the goodbye"
done
sleep 3
room_alive
for i in $(seq 1 $((SEATS - 1))); do
  grep -aq "auto-rejoining room" "$OUT/joiner$i.log" &&
    room_fail "JOINER$i TRIED TO REJOIN AFTER GAME OVER" "joiner$i"
done

room_kill_all
assert_clean "$OUT"/*.log
echo "NSEAT-GAMEOVER-OK seats=$SEATS"
