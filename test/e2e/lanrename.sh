#!/bin/bash
# LAN rejoin when the host comes back under a DIFFERENT name (the iOS
# Game Center alias upgrade makes local_host_name() time-varying — see
# NETPLAY.md "session-frozen with lobby-side drift-follow"). No relay
# (dead signal URL): every pairing runs on the LAN door.
#   Phase 0: A hosts as ORIGNAME (NEWTONIA_DEVICE_NAME override), B
#     joins over the LAN row and bootstraps.
#   Phase 1: A is SIGKILLed. B enters the LAN rejoin lobby browsing for
#     ORIGNAME. A fresh host D comes up as REBORN — auto-rejoin must NOT
#     fire (names differ), but the rejoin wait screen lists the live
#     browse rows, so B joins REBORN manually (Down + Enter) and
#     bootstraps again (2nd "bootstrap adopted").
# Prints LANRENAME-E2E-OK on success.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
export NEWTONIA_SIGNAL_URL="ws://127.0.0.1:9/ws"  # dead port: no relay
export NEWTONIA_LAN_PORT="${NEWTONIA_LAN_PORT:-42641}"
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

export NEWTONIA_DEVICE_NAME=ORIGNAME
PA=$(launch host); sleep 2
unset NEWTONIA_DEVICE_NAME
WA=$(newtonia_windows | head -1)
key $WA Return; sleep 1; key $WA s; key $WA Return; sleep 1; key $WA Return

SNAP=$(newtonia_windows)
PB=$(launch joiner); sleep 3
WB=$(new_window "$SNAP")
key $WB Return; sleep 1; key $WB s; key $WB Return; sleep 1; key $WB s; key $WB Return
wait_log "$OUT/joiner.log" "lan host found: ORIGNAME" 15 || { kill $PA $PB; exit 1; }
key $WB Down; key $WB Return
wait_log "$OUT/joiner.log" "bootstrap adopted" 25 || { kill $PA $PB; exit 1; }
echo "== phase 0: LAN pair up as ORIGNAME OK"

kill -9 $PA
echo "== phase 1: host killed; client must browse for ORIGNAME"
wait_log "$OUT/joiner.log" "auto-rejoining lan host ORIGNAME" 60 || { kill $PB; exit 1; }

SNAP=$(newtonia_windows)
export NEWTONIA_DEVICE_NAME=REBORN
PD=$(launch host2); sleep 3
unset NEWTONIA_DEVICE_NAME
WD=$(new_window "$SNAP")
key $WD Return; sleep 1; key $WD s; key $WD Return; sleep 1; key $WD Return
wait_log "$OUT/joiner.log" "lan host found: REBORN" 20 || { kill $PB $PD; exit 1; }

# The names differ, so auto-rejoin must NOT have fired: give it a few
# beacons' worth of chances, then assert the browse is still waiting.
sleep 4
if grep -aq "lan rejoin: .* reappeared" "$OUT/joiner.log"; then
  echo "FAIL: auto-rejoin fired on a different name"; kill $PB $PD; exit 1
fi
n=$(grep -ac "bootstrap adopted" "$OUT/joiner.log" || true)
[ "${n:-0}" -eq 1 ] || { echo "FAIL: unexpected 2nd bootstrap"; kill $PB $PD; exit 1; }
shot $WB rejoin_rows

# Manual escape hatch: the rejoin wait screen lists REBORN — join it.
key $WB Down; key $WB Return
wait_log "$OUT/host2.log" "lan joiner completed" 25 || { kill $PB $PD; exit 1; }
wait_log "$OUT/joiner.log" "bootstrap adopted" 25 2 || { kill $PB $PD; exit 1; }
alive $PD host2; alive $PB joiner
echo "== phase 1 OK: manual join of the renamed host from the rejoin screen"

sleep 2
kill $PB $PD 2>/dev/null; wait $PB $PD 2>/dev/null
assert_clean "$OUT/host.log" "$OUT/joiner.log" "$OUT/host2.log"
echo "LANRENAME-E2E-OK"
