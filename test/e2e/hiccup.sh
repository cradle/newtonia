#!/bin/bash
# Mid-session transport-death self-repair (the "TURN expiry hiccup"):
# both processes stay ALIVE while the transport dies underneath them —
# simulated by SIGSTOPping the joiner until the host's ICE declares the
# connection failed, then SIGCONTing it. The host must auto-pause and
# reopen the room; the thawed client must AUTO-rejoin in place (fresh
# lobby, fresh relay creds, zero input) and bootstrap a second time.
# This is the recovery path a TURN credential expiry exercises (the
# creds themselves need real Cloudflare TURN — see TESTING.md).
# Prints HICCUP-E2E-OK.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

PA=$(launch host); sleep 2
PB=$(launch joiner); sleep 4
WINS=$(newtonia_windows); A=$(echo "$WINS" | head -1); B=$(echo "$WINS" | tail -1)
[ "$A" != "$B" ] || { echo "only one window"; exit 1; }

key $A Return; sleep 1; key $A s; key $A Return; sleep 1; key $A Return
CODE=$(host_room_code host)
[ -n "$CODE" ] || { echo "NO ROOM CODE"; kill $PA $PB; exit 1; }
echo "room code: $CODE"
key $B Return; sleep 1; key $B s; key $B Return; sleep 1; key $B s; key $B Return; sleep 1
for c in $(echo "$CODE" | grep -o .); do key $B "$c"; done
for i in $(seq 1 22); do
  grep -aq "bootstrap adopted" "$OUT/joiner.log" && break; sleep 1
done
alive $PA host; alive $PB joiner
grep -aq "bootstrap adopted" "$OUT/joiner.log" || { echo "NO FIRST BOOTSTRAP"; exit 1; }

echo "== freeze the joiner until the host's transport fails"
kill -STOP $PB
LOST=""
for i in $(seq 1 60); do
  sleep 1; alive $PA host
  grep -aq "player 2 lost" "$OUT/host.log" && { LOST=1; break; }
done
[ -n "$LOST" ] || { echo "HOST NEVER DETECTED THE LOSS"; kill -CONT $PB; kill $PA $PB; exit 1; }
grep -aq "paused awaiting rejoin" "$OUT/host.log" || { echo "NO AUTO-PAUSE"; kill -CONT $PB; kill $PA $PB; exit 1; }
echo "== host parked + paused; thaw the joiner"
kill -CONT $PB

REJOINED=""
for i in $(seq 1 45); do
  sleep 1; alive $PA host; alive $PB joiner
  grep -aq "player 2 rejoined" "$OUT/host.log" && { REJOINED=1; break; }
done
[ -n "$REJOINED" ] || { echo "NO SELF-REPAIR"; kill $PA $PB; exit 1; }
grep -aq "auto-rejoining room" "$OUT/joiner.log" || { echo "REJOIN WAS NOT AUTOMATIC"; kill $PA $PB; exit 1; }
[ "$(grep -ac 'bootstrap adopted' "$OUT/joiner.log")" -ge 2 ] || { echo "NO SECOND BOOTSTRAP"; kill $PA $PB; exit 1; }

# Resume from the host and confirm both keep playing.
key $A p; sleep 2
xdotool keydown --window $B w; sleep 2; xdotool keyup --window $B w
alive $PA host; alive $PB joiner
kill $PA $PB 2>/dev/null; wait $PA $PB 2>/dev/null
assert_clean "$OUT/host.log" "$OUT/joiner.log"
echo "HICCUP-E2E-OK"
