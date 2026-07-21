#!/bin/bash
# LAN door + live relay: the room stays OPEN when the LAN door wins the
# pairing (it used to be killed), so a LAN session keeps the relay
# session's loss toolkit. Needs the local relay (see room.sh). The
# host's auto-copied join URL is CLEARED from the clipboard before the
# joiner opens JOIN — otherwise the code auto-join wins the race and the
# LAN row is never exercised. Asserts: the keep log at adoption, BOTH
# rejoin doors opening on peer loss, and a LAN re-pair into the running
# game. Prints LANKEEP-E2E-OK.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
export NEWTONIA_LAN_PORT="${NEWTONIA_LAN_PORT:-42647}"
. "$(dirname "$0")/lib.sh"
relay_check
command -v xclip >/dev/null || { echo "SKIP: xclip not installed"; exit 0; }

wait_log() {  # $1=file $2=pattern $3=timeout_s
  local i
  for i in $(seq 1 "$3"); do
    sleep 1
    grep -aq "$2" "$1" && return 0
  done
  echo "TIMEOUT waiting for '$2' in $1"
  return 1
}

PA=$(launch host); sleep 2
WA=$(newtonia_windows | head -1)
key $WA Return; sleep 1; key $WA s; key $WA Return; sleep 1; key $WA Return
CODE=$(host_room_code host)
[ -n "$CODE" ] || { echo "NO ROOM CODE"; kill $PA; exit 1; }
echo "room: $CODE"
printf '' | xclip -selection clipboard -i   # kill the code auto-join race

PB=$(launch joiner); sleep 3
WB=$(newtonia_windows | tail -1)
key $WB Return; sleep 1; key $WB s; key $WB Return; sleep 1; key $WB s; key $WB Return
wait_log "$OUT/joiner.log" "lan host found" 15 || { kill $PA $PB; exit 1; }
key $WB Down; key $WB Return
wait_log "$OUT/host.log" "lan door won - keeping room $CODE" 25 || { kill $PA $PB; exit 1; }
wait_log "$OUT/joiner.log" "bootstrap adopted" 25 || { kill $PA $PB; exit 1; }
echo "== LAN pairing adopted with the room kept open"

kill -9 $PB
echo "== joiner killed; BOTH rejoin doors must open"
wait_log "$OUT/host.log" "room $CODE reopened for rejoin" 30 || { kill $PA; exit 1; }
wait_log "$OUT/host.log" "lan door reopened for rejoin" 10 || { kill $PA; exit 1; }

SNAP=$(newtonia_windows)
PC=$(launch joiner2); sleep 3
WC=""
for w in $(newtonia_windows); do echo "$SNAP" | grep -q "$w" || WC=$w; done
printf '' | xclip -selection clipboard -i   # the disconnect re-copies nothing, but stay deterministic
key $WC Return; sleep 1; key $WC s; key $WC Return; sleep 1; key $WC s; key $WC Return
wait_log "$OUT/joiner2.log" "lan host found" 15 || { kill $PA $PC; exit 1; }
key $WC Down; key $WC Return
wait_log "$OUT/host.log" "player 2 rejoined" 30 || { kill $PA $PC; exit 1; }
wait_log "$OUT/joiner2.log" "bootstrap adopted" 20 || { kill $PA $PC; exit 1; }
alive $PA host; alive $PC joiner2

kill $PA $PC 2>/dev/null; wait $PA $PC 2>/dev/null
assert_clean "$OUT/host.log" "$OUT/joiner.log" "$OUT/joiner2.log"
echo "LANKEEP-E2E-OK"
