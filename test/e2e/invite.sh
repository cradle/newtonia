#!/bin/bash
# Invite re-advertise + teardown: connect, SIGKILL the joiner (host should
# advertise the open slot), then send the host the menu key (Esc) and verify
# the join advertisement is cleared on GLGame teardown.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

PA=$(launch host); sleep 2
PB=$(launch joiner); sleep 4
WINS=$(newtonia_windows)
A=$(echo "$WINS" | head -1); B=$(echo "$WINS" | tail -1)
[ "$A" != "$B" ] || { echo "only one window"; exit 1; }

key $A Return; sleep 1; key $A s; key $A Return; sleep 1; key $A Return
CODE=$(host_room_code host)
[ -n "$CODE" ] || { echo "NO ROOM CODE"; kill $PA $PB; exit 1; }
echo "room code: $CODE"
nav_join $B "$CODE" joiner
echo "== waiting for connect"; sleep 18
alive $PA host; alive $PB joiner

echo "== SIGKILL joiner -> host should advertise joinable"
kill -9 $PB; sleep 14
alive $PA host

echo "== host quits via the pause menu (teardown clears the join)"
# The loss auto-paused the host awaiting the rejoin, so the pause menu is
# ALREADY up — Esc would resume, not quit. Navigate to RETURN TO MENU
# (s clamps past the end of the 3-row menu).
key $A s; key $A s; key $A s; key $A Return; sleep 3
alive $PA host

kill $PA 2>/dev/null; wait $PA 2>/dev/null
echo "== invite transitions on host =="
grep -a "net: invite" "$OUT/host.log"
if grep -aq "joinable (peer gone)" "$OUT/host.log" && \
   grep -aq "no longer joinable (game teardown)" "$OUT/host.log"; then
  echo "INVITE-TEARDOWN-OK"
else
  echo "INVITE-TEARDOWN-FAIL"
fi
