#!/bin/bash
# LAN mid-game rejoin, both directions (task #163, round 4). No relay
# (dead signal URL) throughout — every recovery runs on the LAN door.
#   Phase 1 (host door): A hosts, B joins over LAN, B is SIGKILLed. A
#     must reopen the LAN door (re-beacon + fresh blob) and pause. A NEW
#     instance C discovers A on the JOIN screen and re-pairs into the
#     running game ("lan rejoiner completed", "player 2 rejoined").
#   Phase 2 (client rediscovery): A is SIGKILLed. C must enter the LAN
#     rejoin lobby ("auto-rejoining lan host") and browse for the name.
#     A fresh instance D hosts (same machine name) — C auto-pairs into
#     whatever D hosts and bootstraps again (2nd "bootstrap adopted").
# Prints LANREJOIN-E2E-OK on success.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
export NEWTONIA_SIGNAL_URL="ws://127.0.0.1:9/ws"  # dead port: no relay
export NEWTONIA_LAN_PORT="${NEWTONIA_LAN_PORT:-42637}"
. "$(dirname "$0")/lib.sh"

wait_log() {  # $1=file $2=pattern $3=timeout_s [$4=min_count]
  local i n
  for i in $(seq 1 "$3"); do
    sleep 1
    n=$(grep -ac "$2" "$1" || true)
    [ "${n:-0}" -ge "${4:-1}" ] && return 0
  done
  echo "TIMEOUT waiting for '$2' in $1"
  return 1
}

new_window() {  # window id not in $1 (the previous snapshot)
  local w
  for w in $(newtonia_windows); do
    echo "$1" | grep -q "$w" || { echo "$w"; return; }
  done
}

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
echo "== phase 0: LAN pair up OK"

kill -9 $PB
echo "== phase 1: joiner killed; host must reopen the LAN door"
wait_log "$OUT/host.log" "lan door reopened for rejoin" 30 || { kill $PA; exit 1; }
grep -aq "paused awaiting rejoin" "$OUT/host.log" || { echo "NO PAUSE"; kill $PA; exit 1; }

SNAP=$(newtonia_windows)
PC=$(launch joiner2); sleep 3
WC=$(new_window "$SNAP")
key $WC Return; sleep 1; key $WC s; key $WC Return; sleep 1; key $WC s; key $WC Return
wait_log "$OUT/joiner2.log" "lan host found" 15 || { kill $PA $PC; exit 1; }
key $WC Down; key $WC Return
wait_log "$OUT/host.log" "lan rejoiner completed" 25 || { kill $PA $PC; exit 1; }
wait_log "$OUT/host.log" "player 2 rejoined" 20 || { kill $PA $PC; exit 1; }
wait_log "$OUT/joiner2.log" "bootstrap adopted" 20 || { kill $PA $PC; exit 1; }
alive $PA host; alive $PC joiner2
echo "== phase 1 OK: new joiner re-paired into the running game"

kill -9 $PA
echo "== phase 2: host killed; client must browse for it by name"
# Loss detection on a hard kill rides the client's RX watchdog — allow it
# time, then the 1.5 s auto-rejoin countdown hands over to the browse.
wait_log "$OUT/joiner2.log" "auto-rejoining lan host" 60 || { kill $PC; exit 1; }

SNAP=$(newtonia_windows)
PD=$(launch host2); sleep 3
WD=$(new_window "$SNAP")
key $WD Return; sleep 1; key $WD s; key $WD Return; sleep 1; key $WD Return
wait_log "$OUT/joiner2.log" "lan rejoin: .* reappeared" 20 || { kill $PC $PD; exit 1; }
wait_log "$OUT/host2.log" "lan joiner completed" 25 || { kill $PC $PD; exit 1; }
wait_log "$OUT/joiner2.log" "bootstrap adopted" 25 2 || { kill $PC $PD; exit 1; }
alive $PD host2; alive $PC joiner2
echo "== phase 2 OK: client rediscovered a fresh host by name"

sleep 2
kill $PC $PD 2>/dev/null; wait $PC $PD 2>/dev/null
assert_clean "$OUT/host.log" "$OUT/joiner2.log" "$OUT/host2.log"
echo "LANREJOIN-E2E-OK"
