#!/bin/bash
# Spectator terminal-disconnect regression: the joiner is emptied of lives and
# enters spectator mode watching the host; then the HOST PROCESS IS KILLED.
# Because the joiner is already out there is nothing to rejoin for, so it must
# land on the GAME OVER card (not the REJOINING spinner). Asserts the log line
# and the card; prints SPECTATE-DISC-E2E-OK on success. See TESTING.md.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

# Host empties the joiner's lives 20 s into 2-player play.
PA=$(NEWTONIA_NET_TEST_KILL_MS=20000 NEWTONIA_NET_TEST_KILL_WHO=remote launch host)
sleep 2
PB=$(launch joiner)
sleep 4

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
grep -aq "bootstrap adopted" "$OUT/joiner.log" || { echo "NO BOOTSTRAP"; exit 1; }

echo "== waiting for kill hook (joiner out of lives)"
for i in $(seq 1 30); do
  grep -aq "TEST forcing remote player out" "$OUT/host.log" && break
  sleep 1
done
grep -aq "TEST forcing remote player out" "$OUT/host.log" || {
  echo "KILL HOOK DID NOT FIRE"; kill $PA $PB; exit 1; }

# The host's hook line only says the host applied the death — the JOINER has
# to receive it and arm its own countdown before any of this means anything.
# Wait for that (revive.sh waits on the same marker for the same reason),
# then let the 5 s countdown elapse so it is actively spectating.
for i in $(seq 1 30); do
  grep -aq "spectate armed" "$OUT/joiner.log" && break; sleep 1
done
grep -aq "spectate armed" "$OUT/joiner.log" || {
  echo "FAIL: joiner never armed spectate"; kill $PA $PB; exit 1; }
sleep 7
shot $B disc-spectating          # expect "SPECTATING"

echo "== killing the host mid-spectate"
kill -9 $PA 2>/dev/null

# The joiner must detect the loss and, being already out, jump to GAME OVER —
# NOT enter the REJOINING flow. The 10 s RX watchdog has to expire first, and
# the whole budget was 20 s: fine on a quiet box, and not on a CI runner,
# where this failed on the first attempt of run 31672220859 and passed on the
# retry. Sixty, like revive.sh's arm poll: the loop returns the instant the
# line lands, so the headroom is only spent when the machine is actually slow.
for i in $(seq 1 60); do
  grep -aq "host lost while spectating - GAME OVER" "$OUT/joiner.log" && break
  sleep 1
done
alive $PB joiner
grep -aq "host lost while spectating - GAME OVER" "$OUT/joiner.log" || {
  echo "FAIL: joiner did not take the terminal GAME OVER path"; kill $PB; exit 1; }
# And it must NOT have auto-rejoined.
grep -aq "auto-rejoining room" "$OUT/joiner.log" && {
  echo "FAIL: joiner auto-rejoined instead of GAME OVER"; kill $PB; exit 1; }
sleep 1
shot $B disc-gameover            # expect the GAME OVER card

kill $PB 2>/dev/null; wait $PA $PB 2>/dev/null
assert_clean "$OUT/joiner.log"
echo "SPECTATE-DISC-E2E-OK"
