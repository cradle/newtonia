#!/bin/bash
# Same-machine auto-join probe regression: the host instance persists its
# room code into the SHARED prefs (lib.sh gives both instances one
# XDG_DATA_HOME, like two apps on one mac) and copies it to the shared X
# clipboard; a joiner launched AFTERWARDS loads that pref at startup and
# must still auto-join via the own-room probe — with zero typing.
# Prints OWNROOM-E2E-OK. Guards the (r) probe against regressing to the
# old hard refusal.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

# Shut the LAN door for this run. Both instances live on one box, so the host
# beacons and the joiner sees a LAN row — and since the one-box clipboard race
# was fixed (lanclip.sh) the lobby deliberately HOLDS a clipboard code while
# LAN rows are present ("own-room code on clipboard held - lan rows first").
# That precedence is correct and lanclip.sh guards it; it just makes the
# clipboard probe unreachable here, which is the ONLY door this driver is
# about. lan_visible=0 (lan_hidden.sh's pref) leaves the probe as the only way
# in, restoring the scenario this driver was written for.
PREFDIR="$OUT/xdg/cc.gfm/newtonia"
mkdir -p "$PREFDIR"
printf 'lan_visible=0\n' > "$PREFDIR/preferences.ini"

PA=$(launch host); sleep 3
A=$(newtonia_windows | head -1)
key $A Return; sleep 1; key $A s; key $A Return; sleep 1; key $A Return
CODE=$(host_room_code host)
[ -n "$CODE" ] || { echo "NO ROOM CODE"; kill $PA; exit 1; }
echo "room code: $CODE"
sleep 1   # prefs + clipboard written

PB=$(launch joiner); sleep 4   # joiner loads prefs WITH the live code
for w in $(newtonia_windows); do [ "$w" != "$A" ] && B=$w; done
key $B Return; sleep 1; key $B s; key $B Return; sleep 1; key $B s; key $B Return
echo "== no typing - waiting for the clipboard auto-join probe"; sleep 15
alive $PA host; alive $PB joiner
kill $PA $PB 2>/dev/null; wait $PA $PB 2>/dev/null
assert_clean "$OUT/host.log" "$OUT/joiner.log"
grep -aq "bootstrap adopted" "$OUT/joiner.log" && echo "OWNROOM-E2E-OK" || { echo "OWNROOM-E2E-FAIL"; grep -a lobby "$OUT/joiner.log" | tail -4; exit 1; }
