#!/bin/bash
# REAL TURN credential-expiry verification. Needs a machine with normal
# internet (UDP egress) — the dev container has none. One-time setup:
#   cd signal
#   npx wrangler secret put TURN_TTL      # enter e.g. 90 (seconds)
#   npx wrangler deploy
# ...run this..., then restore:
#   npx wrangler secret delete TURN_TTL && npx wrangler deploy
#
# Both instances run relay-only (NEWTONIA_NET_FORCE_RELAY=1), so the
# whole session flows through Cloudflare TURN on the tiny-TTL creds:
# connect, assert "ice path seat 2 relay/relay", idle until the credential
# expiry kills the allocation (expiry + one allocation lifetime,
# typically 5-15 min), then assert the auto-pause -> AUTO-rejoin
# self-repair — the rejoin mints FRESH creds, so it must reconnect even
# relay-only. Prints TURNEXPIRY-E2E-OK. Budget ~25 min.
set -u
export NEWTONIA_SIGNAL_URL="${NEWTONIA_SIGNAL_URL:-wss://newtonia-signal.gfmcc.workers.dev/ws}"
export NEWTONIA_NET_FORCE_RELAY=1
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
echo "room code: $CODE (relay-only, tiny-TTL creds)"
key $B Return; sleep 1; key $B s; key $B Return; sleep 1; key $B s; key $B Return; sleep 1
for c in $(echo "$CODE" | grep -o .); do key $B "$c"; done
for i in $(seq 1 30); do
  grep -aq "bootstrap adopted" "$OUT/joiner.log" && break; sleep 1
done
alive $PA host; alive $PB joiner
grep -aq "bootstrap adopted" "$OUT/joiner.log" || { echo "NO BOOTSTRAP"; kill $PA $PB; exit 1; }
grep -a "ice path" "$OUT/host.log" "$OUT/joiner.log"
grep -aq "ice path seat 2 relay/relay" "$OUT/host.log" || {
  echo "NOT RELAYED - is TURN minting creds? (check TURN_KEY_ID/TURN_API_TOKEN)"; kill $PA $PB; exit 1; }

echo "== relayed and playing; waiting out the credential expiry (up to 20 min)"
LOST=""
for i in $(seq 1 240); do
  sleep 5; alive $PA host; alive $PB joiner
  grep -aq "player 2 lost" "$OUT/host.log" && { LOST=1; break; }
done
[ -n "$LOST" ] || { echo "TRANSPORT NEVER DIED - TTL override active?"; kill $PA $PB; exit 1; }
echo "== transport died after expiry; awaiting self-repair"

REJOINED=""
for i in $(seq 1 60); do
  sleep 2; alive $PA host; alive $PB joiner
  grep -aq "player 2 rejoined" "$OUT/host.log" && { REJOINED=1; break; }
done
[ -n "$REJOINED" ] || { echo "NO SELF-REPAIR"; kill $PA $PB; exit 1; }
grep -aq "auto-rejoining room" "$OUT/joiner.log" || { echo "REJOIN WAS NOT AUTOMATIC"; kill $PA $PB; exit 1; }
[ "$(grep -ac 'ice path seat 2 relay/relay' "$OUT/host.log")" -ge 2 ] || echo "note: repaired path was not relay/relay (check second 'ice path' line)"

kill $PA $PB 2>/dev/null; wait $PA $PB 2>/dev/null
assert_clean "$OUT/host.log" "$OUT/joiner.log"
echo "TURNEXPIRY-E2E-OK"
