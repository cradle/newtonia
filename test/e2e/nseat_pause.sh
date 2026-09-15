#!/bin/bash
# Shared pause/resume must reach every seat, including uninvolved clients.
# Exercise each client and the host as initiator, with another seat resuming.
set -u
SEATS="${SEATS:-4}"
[ "$SEATS" -ge 3 ] && [ "$SEATS" -le 4 ] || { echo "SEATS must be 3 or 4"; exit 1; }
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check
export NEWTONIA_NET_TEST_SEATS=$SEATS
room_setup "$SEATS" NEWTONIA_GOD=1
trap room_kill_all EXIT

# Wait for the first-INPUT resync before exercising session controls.
sleep 2
transitions=0
assert_state() {
  local state=$1 i name count last ready
  for _ in $(seq 1 30); do
    room_alive
    ready=1
    for i in "${!ROOM_PIDS[@]}"; do
      name=$(room_name "$i")
      count=$(grep -ac 'net: pause state ' "$OUT/$name.log" || true)
      last=$(grep -a 'net: pause state ' "$OUT/$name.log" | tail -1)
      [ "$count" = "$transitions" ] && [[ "$last" == *"state $state" ]] || ready=
    done
    [ -n "$ready" ] && return
    sleep 0.1
  done
  for i in "${!ROOM_PIDS[@]}"; do
    name=$(room_name "$i")
    echo "== $name pause history"
    grep -a 'net: pause state ' "$OUT/$name.log" || true
  done
  room_fail "Not every seat reached $state after $transitions transitions"
}

# Start with a client: the old code pauses it and the host, but leaves
# the bystanders running. Later rounds also cover host-initiated control.
for initiator in $(seq 1 $((SEATS - 1))) 0; do
  echo "== $(room_name "$initiator") pauses"
  key "${ROOM_WINS[$initiator]}" p
  room_alive
  transitions=$((transitions + 1))
  assert_state paused
  sleep 1
  assert_state paused  # no echo may toggle anyone back or repeat the transition

  resumer=$(((initiator + 1) % SEATS))
  echo "== $(room_name "$resumer") resumes"
  key "${ROOM_WINS[$resumer]}" p
  room_alive
  transitions=$((transitions + 1))
  assert_state running
  sleep 1
  assert_state running
done

room_kill_all
trap - EXIT
assert_clean "$OUT"/*.log
echo "NSEAT-PAUSE-OK seats=$SEATS"
