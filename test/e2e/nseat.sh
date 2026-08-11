#!/bin/bash
# B6 N-seat connect/play smoke (SEATS=3|4, default 4): host + SEATS-1
# relay joiners assemble through the waiting room, the room auto-starts
# when the last seat fills, every joiner bootstraps from the snapshot,
# and everyone flies + fires together. Prints "NSEAT-SMOKE-OK seats=N".
# Needs a local relay (see lib.sh). See TESTING.md.
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

echo "== everyone flies + fires for 8s"
fly_all 8
sleep 2
room_alive

# Seat-identity relay (MSG_PEER_IDENT): once every client has sent INPUT,
# each one must have been told about every OTHER client's seat (a client's
# OWN seat is deliberately never stored). joiner I sits on seat I+1; the
# relay fires on each peer's first INPUT, which the flight above
# guarantees has happened for all of them.
for i in $(seq 1 $((SEATS - 1))); do
  my=$((i + 1))
  for s in $(seq 2 "$SEATS"); do
    [ "$s" = "$my" ] && continue
    grep -aq "net: seat $s identity relay" "$OUT/joiner$i.log" ||
      room_fail "joiner$i never learned seat $s identity" "joiner$i"
  done
done
for i in "${!ROOM_WINS[@]}"; do
  shot "${ROOM_WINS[$i]}" "nseat$SEATS-$(room_name "$i")"
done

room_kill_all
assert_clean "$OUT"/*.log
echo "NSEAT-SMOKE-OK seats=$SEATS"
