#!/bin/bash
# ALLOW ANONYMOUS PLAYERS NO closes the LAN door COMPLETELY (FOURPLAYER.md
# O3): a LAN peer can never be attested — no worker on that path — so under
# NO the host must not beacon at all. It used to stay open on the "a LAN
# peer was invited" premise, and a BANNED pilot renamed and walked straight
# back in through it (field, two Steam accounts, 2026-08-30).
#
# Three beats, one host, driven off the greppable door logs:
#   1. cold start with the pref preseeded to NO -> no "lan announce up"
#      at the HOST commit;
#   2. flip the waiting room's policy row to YES -> the door opens
#      ("lan announce up" appears);
#   3. flip back to NO -> "lan door closed (anonymous players disabled)",
#      and no further announce.
# Needs a live relay for the waiting room (the toggle lives there), but a
# PLAIN one — no attestation involved — so it self-hosts wrangler dev
# without FAKE_VERIFY. Prints LAN-ANON-OK on success.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
cd "$(dirname "$0")"

PORT="${LAN_ANON_RELAY_PORT:-8793}"
export NEWTONIA_SIGNAL_URL="ws://127.0.0.1:$PORT/ws"
export NEWTONIA_LAN_PORT="${NEWTONIA_LAN_PORT:-42623}"
( cd ../../signal && exec npx wrangler@4 dev --local --port "$PORT" ) \
  > /tmp/lan_anon_wrangler.log 2>&1 &
WPID=$!
trap 'kill_tree $WPID 2>/dev/null || kill $WPID 2>/dev/null' EXIT
echo "== starting relay on :$PORT (pid $WPID)"
for _ in $(seq 1 90); do
  curl -s -o /dev/null "http://127.0.0.1:$PORT/" && break
  kill -0 $WPID 2>/dev/null || { echo "relay died:"; cat /tmp/lan_anon_wrangler.log; exit 1; }
  sleep 1
done

. ./lib.sh
relay_check
export NEWTONIA_NET_TEST_SEATS=4   # a room that stays open, not one that starts

# Preseed the policy to NO (this run's isolated XDG dir): the cold-start
# leg is the commit-time gate, not the toggle.
PREFDIR="$OUT/xdg/cc.gfm/newtonia"
mkdir -p "$PREFDIR"
printf 'allow_anonymous=0\n' > "$PREFDIR/preferences.ini"

ROOM_PIDS=(); ROOM_CODE=
ROOM_PIDS[0]=$(launch host)
sleep 3
HW=$(newtonia_windows | head -1)
[ -n "$HW" ] || room_fail "NO HOST WINDOW" host
nav_host "$HW"
ROOM_CODE=$(host_room_code host)
[ -n "$ROOM_CODE" ] || room_fail "NO ROOM CODE" host
echo "room code: $ROOM_CODE"

echo "== 1. preseeded NO: the HOST commit must not open the LAN door"
sleep 2
grep -aq "lan announce up" "$OUT/host.log" &&
  room_fail "LAN DOOR OPENED UNDER ANONYMOUS=NO" host

echo "== 2. flip to YES: the door opens"
# With nobody seated the policy row is the ONLY row, so it already has the
# cursor — right toggles it (nseat_anon's pattern).
xdotool key --window "$HW" d; sleep 1
ok=
for _ in $(seq 1 10); do
  grep -aq "allow anonymous players: YES" "$OUT/host.log" &&
    grep -aq "lan announce up" "$OUT/host.log" && { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "DOOR DID NOT OPEN ON THE FLIP TO YES" host

echo "== 3. flip back to NO: the door closes and stays closed"
xdotool key --window "$HW" d; sleep 1
ok=
for _ in $(seq 1 10); do
  grep -aq "lan door closed (anonymous players disabled)" "$OUT/host.log" &&
    { ok=1; break; }
  sleep 1
done
[ -n "$ok" ] || room_fail "DOOR DID NOT CLOSE ON THE FLIP TO NO" host
sleep 3
[ "$(grep -ac 'lan announce up' "$OUT/host.log")" = 1 ] ||
  room_fail "DOOR RE-OPENED AFTER THE FLIP TO NO" host

room_alive
room_kill_all
echo "LAN-ANON-OK (closed cold, opened on YES, closed on NO)"
