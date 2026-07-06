#!/bin/bash
# Version-detection regression: a fake OLD host (node, sends a pv-less
# offer before the joiner arrives — exercising the worker's stored-offer
# replay) vs a real game joiner. The joiner must fail INSTANTLY with the
# VERSION MISMATCH screen instead of a 25 s ICE timeout. Prints
# MISMATCH-E2E-OK. Requires node on PATH.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

node "$(dirname "$0")/fake_old_host.mjs" > "$OUT/fakehost.log" 2>&1 &
FH=$!
CODE=""
for i in $(seq 1 10); do
  sleep 1
  CODE=$(grep -a CODE "$OUT/fakehost.log" | awk '{print $2}')
  [ -n "$CODE" ] && break
done
[ -n "$CODE" ] || { echo "NO FAKE ROOM"; kill $FH; exit 1; }
echo "fake old-build room: $CODE"

PB=$(launch joiner); sleep 3
B=$(newtonia_windows | head -1)
key $B Return; sleep 1; key $B s; key $B Return; sleep 1; key $B s; key $B Return; sleep 1
for c in $(echo "$CODE" | grep -o .); do key $B "$c"; done
sleep 5
alive $PB joiner
shot $B mismatch-screen
kill $PB $FH 2>/dev/null; wait $PB $FH 2>/dev/null
grep -aq "version mismatch" "$OUT/joiner.log" && echo "MISMATCH-E2E-OK" || { echo "MISMATCH-E2E-FAIL"; exit 1; }
