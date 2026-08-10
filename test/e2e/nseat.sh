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
for i in "${!ROOM_WINS[@]}"; do
  shot "${ROOM_WINS[$i]}" "nseat$SEATS-$(room_name "$i")"
done

room_kill_all
assert_clean "$OUT"/*.log
echo "NSEAT-SMOKE-OK seats=$SEATS"
