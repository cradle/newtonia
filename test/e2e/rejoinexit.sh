#!/bin/bash
# The auto-rejoin wait screen ("WAITING FOR THE HOST TO COME BACK") must
# answer like a menu: its BACK TO MENU row carries the shared cursor and
# confirm gives up the rejoin, Esc likewise (Glenn: the row drew as a dead
# label that Enter could not select, 2026-08-07). Pairs host+joiner via
# the relay, SIGKILLs the HOST (dirty loss, room code stays live), waits
# for the joiner's auto-rejoin lobby, and exits — round 1 via Enter
# (confirm), round 2 via Esc. Prints REJOINEXIT-E2E-OK on success.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

wait_log() {  # $1=file $2=pattern $3=timeout_s
  local i
  for i in $(seq 1 "$3"); do
    grep -aq "$2" "$1" && return 0
    sleep 1
  done
  echo "TIMEOUT waiting for '$2' in $1"; return 1
}

round() {  # $1 = round name, $2 = exit key (Return / Escape)
  local NAME=$1 KEY=$2
  echo "===== round $NAME: exit via $KEY ====="
  # Fresh prefs per round: a savegame left by the previous round adds a
  # CONTINUE row and shifts the menu under the fixed key script.
  export XDG_DATA_HOME="$OUT/xdg-$NAME"
  PA=$(launch host-$NAME)
  sleep 2
  PB=$(launch joiner-$NAME)
  sleep 4
  WINS=$(newtonia_windows)
  A=$(echo "$WINS" | head -1); B=$(echo "$WINS" | tail -1)
  [ "$A" != "$B" ] || { echo "only one window"; exit 1; }

  nav_host $A
  CODE=$(host_room_code host-$NAME)
  [ -n "$CODE" ] || { echo "NO ROOM CODE"; kill $PA $PB; exit 1; }
  echo "room code: $CODE"
  nav_join $B "$CODE"
  echo "== waiting for connect"; sleep 15
  alive $PA host-$NAME; alive $PB joiner-$NAME
  grep -aq "bootstrap adopted" "$OUT/joiner-$NAME.log" || {
    echo "NO BOOTSTRAP"; kill $PA $PB; exit 1; }

  echo "== SIGKILL the host (dirty loss, room code stays live)"
  kill -9 $PA 2>/dev/null
  wait_log "$OUT/joiner-$NAME.log" "auto-rejoining room" 30 || {
    kill $PB; exit 1; }
  sleep 3   # let the rejoin lobby settle on the WAITING screen
  alive $PB joiner-$NAME
  shot $B rejoinexit-$NAME-waiting

  echo "== pressing $KEY on the rejoin wait screen"
  key $B $KEY
  sleep 3
  alive $PB joiner-$NAME
  shot $B rejoinexit-$NAME-after
  # The menu visit must come AFTER the rejoin wait (the initial menu pass
  # also logs one).
  if ! awk '/auto-rejoining room/{f=1} f && /Presence: In the Menu/{ok=1} \
      END{exit ok?0:1}' "$OUT/joiner-$NAME.log"; then
    echo "FAIL($NAME): $KEY did not return to the menu"
    kill $PB 2>/dev/null; exit 1
  fi
  echo "== $NAME OK: $KEY left the rejoin wait for the menu"
  kill $PB 2>/dev/null; wait $PB 2>/dev/null
  wait_no_windows
}

round enter Return
round esc Escape
echo "REJOINEXIT-E2E-OK"
