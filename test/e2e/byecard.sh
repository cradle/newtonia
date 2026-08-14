#!/bin/bash
# The terminal disconnect card answers like a menu, not like any key.
# A hosts over LAN, B joins, then A leaves to the menu — a deliberate BYE,
# so B has nothing to rejoin and gets the RETURN TO MENU card. Verdict:
# a plain movement key must NOT drop B out of the game, and a confirm
# must. Prints BYECARD-E2E-OK on success.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
export NEWTONIA_SIGNAL_URL="ws://127.0.0.1:9/ws"  # dead port: no relay
export NEWTONIA_LAN_PORT="${NEWTONIA_LAN_PORT:-42639}"
. "$(dirname "$0")/lib.sh"

wait_log() {  # $1=file $2=pattern $3=timeout_s
  local i
  for i in $(seq 1 "$3"); do
    sleep 1
    grep -aq "$2" "$1" && return 0
  done
  echo "TIMEOUT waiting for '$2' in $1"; return 1
}
new_window() { local w; for w in $(newtonia_windows); do
  echo "$1" | grep -q "$w" || { echo "$w"; return; }; done; }

PA=$(launch host); sleep 2
WA=$(newtonia_windows | head -1)
key $WA Return; sleep 1; key $WA s; key $WA Return; sleep 1; key $WA Return

SNAP=$(newtonia_windows)
PB=$(launch joiner); sleep 3
WB=$(new_window "$SNAP")
key $WB Return; sleep 1; key $WB s; key $WB Return; sleep 1; key $WB s; key $WB Return
wait_log "$OUT/joiner.log" "lan host found" 15 || { kill $PA $PB; exit 1; }
key $WB Down; key $WB Return
wait_log "$OUT/joiner.log" "bootstrap adopted" 25 || { kill $PA $PB; exit 1; }
echo "== paired up over LAN"

# The host leaves deliberately: online Esc opens the pause menu (pausing
# the joiner too), and RETURN TO MENU sends the BYE and closes the room,
# so the joiner's card is the terminal one (no auto-rejoin). s clamps past
# the end — three presses land the last row of the host's 3-row menu.
key $WA Escape; sleep 1
key $WA s; key $WA s; key $WA s; key $WA Return
sleep 4
alive $PB joiner
shot $WB byecard-card

# A movement key used to be "any key" and would have quit. It must not.
key $WB w; key $WB s; key $WB a
sleep 2
alive $PB joiner
if [ -z "$(xdotool search --name Newtonia | grep -x "$WB" || true)" ]; then
  echo "FAIL: joiner window gone after a movement key"; kill $PA $PB; exit 1
fi
shot $WB byecard-after-movement
echo "== movement keys did not exit the card"

# Confirm must leave to the menu. The menu has no LEVEL text; the card does.
key $WB Return
sleep 3
alive $PB joiner
shot $WB byecard-after-confirm
grep -aq "Presence: In the Menu" "$OUT/joiner.log" || {
  echo "FAIL: confirm did not return to the menu"; kill $PA $PB; exit 1; }
echo "== confirm returned to the menu"

kill $PA $PB 2>/dev/null; wait $PA $PB 2>/dev/null
echo "BYECARD-E2E-OK"
