#!/bin/bash
# A tapped invite / deep link to a DEAD room must fail FAST to the retry
# screen, not sit 60 s on "WAITING FOR THE HOST TO COME BACK" (Glenn,
# Android deep link, 2026-09-15). The invite accept shares the mid-game
# rejoin's connect flow but is flagged cold_invite_ so every failure path
# (the relay's no-such-room, or a bare socket close when the explicit
# error is lost to a native message-before-close race) short-circuits to
# LobbyFailed instead of the rejoin budget's retry loop.
#
# The game consumes a "+connect <CODE>" launch arg exactly as the platform
# deep-link backends do (invites.cpp capture_launch -> note_accepted ->
# Menu::tick -> NetLobby(code, InviteAcceptTag)), so launching with a
# valid-shape code the local relay has never hosted drives the whole path
# with no window interaction. Note: a local relay delivers no-such-room
# cleanly, so this guards the fast-fail CONTRACT (cold invite never
# schedules a rejoin retry or burns the budget); the lost-error race it
# also fixes is field-only and covered by the Closed path in net_lobby.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

# ZZZZZ: 5 chars, all in the code alphabet (so it passes the worker's
# shape check and reaches the room), but never hosted -> no-such-room.
DEAD=ZZZZZ
env "$ROOT/newtonia" +connect "$DEAD" > "$OUT/join.log" 2>&1 & PID=$!

# Connect + relay round-trip is ~1 s; give the GL window and first tick
# room to come up. Far under the 60 s the old bug waited, and past the
# 5 s the old socket-closed retry would first have logged.
sleep 12
alive $PID join
kill $PID 2>/dev/null; wait $PID 2>/dev/null

echo "== lobby log =="
grep -a "\[lobby\]" "$OUT/join.log" || true

fail=0
grep -aq "cold invite failed" "$OUT/join.log" || {
  echo "MISSING: cold invite did not fast-fail"; fail=1; }
# The 60 s-hang markers: a cold invite must NEVER schedule a rejoin retry
# or run the budget down.
if grep -aq "rejoin retry in" "$OUT/join.log" || \
   grep -aq "rejoin gave up" "$OUT/join.log"; then
  echo "REGRESSION: cold invite entered the rejoin retry/budget loop"; fail=1
fi

[ $fail -eq 0 ] && echo "INVITE-DEADROOM-OK" || { echo "INVITE-DEADROOM-FAIL"; exit 1; }
