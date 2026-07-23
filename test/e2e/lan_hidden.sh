#!/bin/bash
# LAN visibility opt-out e2e: with the INI pref lan_visible=0 the host must
# NOT beacon — no "lan announce up", and a joiner running Browse on the same
# loopback never discovers it ("lan host found" absent). The relay is pointed
# at a dead port (as in lan.sh), so the host still falls back to the manual
# invite screen; the ONLY difference from lan.sh is that the LAN door is shut.
# Prints LAN-HIDDEN-OK on success. See NETPLAY.md "LAN-visibility opt-out".
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
export NEWTONIA_SIGNAL_URL="ws://127.0.0.1:9/ws"  # dead port: no relay
export NEWTONIA_LAN_PORT="${NEWTONIA_LAN_PORT:-42619}"
. "$(dirname "$0")/lib.sh"

# Preseed the pref file (in this run's isolated XDG dir) with the opt-out.
PREFDIR="$OUT/xdg/cc.gfm/newtonia"
mkdir -p "$PREFDIR"
printf 'lan_visible=0\n' > "$PREFDIR/preferences.ini"

PA=$(launch host)
sleep 2
PB=$(launch joiner)
sleep 4

WINS=$(newtonia_windows)
A=$(echo "$WINS" | head -1); B=$(echo "$WINS" | tail -1)
[ "$A" != "$B" ] || { echo "only one window"; exit 1; }

# Host: attract -> menu -> ONLINE -> HOST (relay dead -> manual fallback).
key $A Return; sleep 1; key $A s; key $A Return; sleep 1; key $A Return

# Joiner: attract -> menu -> ONLINE -> JOIN (CodeEntry + browse running).
key $B Return; sleep 1; key $B s; key $B Return; sleep 1; key $B s; key $B Return

echo "== watching for a (forbidden) discovery for 15s"
for i in $(seq 1 15); do
  sleep 1
  if grep -aq "lan host found" "$OUT/joiner.log"; then
    echo "FAIL: joiner discovered a host that opted out"; kill $PA $PB; exit 1
  fi
done

alive $PA host; alive $PB joiner

# The host must never have brought its announce up.
if grep -aq "lan announce up" "$OUT/host.log"; then
  echo "FAIL: host beaconed despite lan_visible=0"; kill $PA $PB; exit 1
fi
# ...and the joiner must not have found it.
if grep -aq "lan host found" "$OUT/joiner.log"; then
  echo "FAIL: joiner discovered the opted-out host"; kill $PA $PB; exit 1
fi
# Sanity: the host DID reach the relay-dead manual fallback, proving it got
# to the HOST screen (so the missing beacon is the pref, not a stalled flow).
if ! grep -aq "manual fallback" "$OUT/host.log"; then
  echo "FAIL: host never reached the manual fallback (stalled flow?) - the missing beacon proves nothing"
  kill $PA $PB; exit 1
fi

shot $A lan-hidden-host; shot $B lan-hidden-joiner
kill $PA $PB 2>/dev/null; wait $PA $PB 2>/dev/null
echo "LAN-HIDDEN-OK"
