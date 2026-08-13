#!/bin/bash
# The auto-rejoin wait screen ("WAITING FOR THE HOST TO COME BACK") must
# answer like a menu: its BACK TO MENU row carries the shared cursor and
# confirm gives up the rejoin, Esc likewise (Glenn: the row drew as a dead
# label that Enter could not select, 2026-08-07) — but ONLY on a press the
# lobby itself saw: space is a confirm key AND the default fire key, and a
# fire key held through the disconnect releases into this screen (Glenn:
# releasing fire quit the wait, 2026-08-08). Pairs host+joiner via the
# relay, SIGKILLs the HOST (dirty loss, room code stays live), waits for
# the joiner's auto-rejoin lobby, then: round 1 exits via Enter, round 2
# via Esc, round 3 releases a held fire key (must NOT exit) and then
# leaves on a fresh Enter. Prints REJOINEXIT-E2E-OK on success.
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

# Did the joiner reach the menu AFTER the rejoin wait began? (The initial
# menu pass also logs a presence line, so anchor on the rejoin marker.)
in_menu_after_rejoin() {  # $1 = joiner log
  awk '/auto-rejoining room/{f=1} f && /Presence: In the Menu/{ok=1} \
      END{exit ok?0:1}' "$1"
}

# pair_up NAME: launch host+joiner with fresh prefs, pair via the relay,
# SIGKILL the host, and wait for the joiner's auto-rejoin lobby. Leaves
# PB (joiner pid) and B (joiner window) set for the caller.
pair_up() {
  local NAME=$1
  # Fresh prefs per round: a savegame left by the previous round adds a
  # CONTINUE row and shifts the menu under the fixed key script.
  export XDG_DATA_HOME="$OUT/xdg-$NAME"
  PA=$(launch host-$NAME)
  sleep 2
  PB=$(launch joiner-$NAME)
  sleep 4
  local WINS A
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
}

kill_host_await_lobby() {  # $1 = round name
  echo "== SIGKILL the host (dirty loss, room code stays live)"
  kill -9 $PA 2>/dev/null
  # 60 s, not 30: this is the THIRD pairing this process has assembled, and on
  # a loaded runner the auto-rejoin marker arrived late enough to time out on
  # the held-fire round while the first two rounds passed with room to spare
  # (2026-08-13). The wait returns the instant the line lands.
  wait_log "$OUT/joiner-$1.log" "auto-rejoining room" 60 || {
    kill $PB; exit 1; }
  sleep 3   # let the rejoin lobby settle on the WAITING screen
  alive $PB joiner-$1
}

round() {  # $1 = round name, $2 = exit key (Return / Escape)
  local NAME=$1 KEY=$2
  echo "===== round $NAME: exit via $KEY ====="
  pair_up $NAME
  kill_host_await_lobby $NAME
  shot $B rejoinexit-$NAME-waiting

  echo "== pressing $KEY on the rejoin wait screen"
  key $B $KEY
  sleep 3
  alive $PB joiner-$NAME
  shot $B rejoinexit-$NAME-after
  if ! in_menu_after_rejoin "$OUT/joiner-$NAME.log"; then
    echo "FAIL($NAME): $KEY did not return to the menu"
    kill $PB 2>/dev/null; exit 1
  fi
  echo "== $NAME OK: $KEY left the rejoin wait for the menu"
  kill $PB 2>/dev/null; wait $PB 2>/dev/null
  wait_no_windows
}

heldfire_round() {
  local NAME=heldfire
  echo "===== round $NAME: fire held through the disconnect ====="
  pair_up $NAME

  # Hold fire BEFORE the loss: the press lands in the game, so the lobby
  # never saw it — its release must not confirm the exit.
  echo "== holding fire, then SIGKILL the host"
  xdotool keydown --window $B space
  sleep 1
  kill_host_await_lobby $NAME
  shot $B rejoinexit-$NAME-waiting

  echo "== releasing fire on the rejoin wait screen"
  xdotool keyup --window $B space
  sleep 2
  alive $PB joiner-$NAME
  shot $B rejoinexit-$NAME-released
  if in_menu_after_rejoin "$OUT/joiner-$NAME.log"; then
    echo "FAIL($NAME): releasing held fire exited the rejoin wait"
    kill $PB 2>/dev/null; exit 1
  fi
  echo "== held release swallowed; fresh Enter must still leave"
  key $B Return
  sleep 3
  alive $PB joiner-$NAME
  if ! in_menu_after_rejoin "$OUT/joiner-$NAME.log"; then
    echo "FAIL($NAME): fresh Enter did not leave after the stale release"
    kill $PB 2>/dev/null; exit 1
  fi
  echo "== $NAME OK: stale release ignored, fresh press left"
  kill $PB 2>/dev/null; wait $PB 2>/dev/null
  wait_no_windows
}

round enter Return
round esc Escape
heldfire_round
echo "REJOINEXIT-E2E-OK"
