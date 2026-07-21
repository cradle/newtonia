#!/bin/bash
# The LAN-vs-clipboard race (task #159 round 2, Glenn's one-box mac test):
# the host reaches the manual fallback FIRST, so its INVITE blob is already
# on the shared clipboard when the joiner opens CodeEntry. Without the
# hold, the automatic blob pickup steals the screen into the manual flow
# before the LAN row can appear; with it, the joiner logs the held blob,
# lists the host row, and joins over the LAN door. Both instances share
# one Xvfb display, so SDL's clipboard really does carry the blob across.
# Prints LANCLIP-E2E-OK on success.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
export NEWTONIA_SIGNAL_URL="ws://127.0.0.1:9/ws"  # dead port: no relay
export NEWTONIA_LAN_PORT="${NEWTONIA_LAN_PORT:-42627}"
. "$(dirname "$0")/lib.sh"

PA=$(launch host)
sleep 2

# Host: attract -> menu -> ONLINE -> HOST, then wait out the 12 s signal
# timeout so the manual fallback copies the INVITE blob to the clipboard.
A=$(newtonia_windows | head -1)
key $A Return; sleep 1; key $A s; key $A Return; sleep 1; key $A Return
echo "== waiting for the host's manual fallback (blob on the clipboard)"
BLOB=0
for i in $(seq 1 25); do
  sleep 1
  grep -aq "copied invite code to clipboard" "$OUT/host.log" && { BLOB=1; break; }
done
[ "$BLOB" = 1 ] || { echo "NO HOST BLOB"; kill $PA; exit 1; }

# Joiner: only NOW open CodeEntry — the blob is sitting on the clipboard.
PB=$(launch joiner)
sleep 3
B=$(newtonia_windows | tail -1)
[ "$A" != "$B" ] || { echo "only one window"; kill $PA $PB; exit 1; }
key $B Return; sleep 1; key $B s; key $B Return; sleep 1; key $B s; key $B Return

echo "== the blob must be HELD while the LAN row appears"
FOUND=0
for i in $(seq 1 15); do
  sleep 1
  grep -aq "lan host found" "$OUT/joiner.log" && { FOUND=1; break; }
done
[ "$FOUND" = 1 ] || { echo "NO LAN DISCOVERY"; kill $PA $PB; exit 1; }
sleep 2   # give the repoll a couple more cracks at the held blob
if grep -aq "manual invite found at code entry" "$OUT/joiner.log"; then
  echo "RACE LOST: auto blob pickup beat the LAN row"; kill $PA $PB; exit 1
fi
# The held log proves the clipboard really delivered the blob AND the
# hold suppressed it (not just that the clipboard never arrived).
grep -aq "invite blob on clipboard held" "$OUT/joiner.log" ||
  { echo "NO HELD BLOB (clipboard never delivered?)"; kill $PA $PB; exit 1; }
shot $B lanclip-codeentry

# Join via the LAN row; the pairing must come up on host candidates.
key $B Down; key $B Return
echo "== waiting for the LAN pairing + connect"
sleep 20
alive $PA host; alive $PB joiner
grep -aq "lan answer received" "$OUT/host.log" || { echo "NO ANSWER"; exit 1; }
grep -aq "bootstrap adopted" "$OUT/joiner.log" || { echo "NO BOOTSTRAP"; exit 1; }

kill $PA $PB 2>/dev/null; wait $PA $PB 2>/dev/null
assert_clean "$OUT/host.log" "$OUT/joiner.log"
echo "LANCLIP-E2E-OK"
