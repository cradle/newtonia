#!/bin/bash
# Host process-death resume e2e (NETPLAY.md): connect via room code, march
# one level, SIGKILL the HOST mid-game, relaunch it within the reclaim
# grace and drive the menu's RESUME HOSTING row. Asserts the ticket +
# online save exist while hosting, the relaunched host reclaims the room
# and re-enters the world at the recorded level ("Presence: Level 2
# Co-Op" — generation survived via the online save), the client's
# auto-rejoin reconnects ("player 2 rejoined"), and a clean quit deletes
# both resume files. Per-instance XDG_DATA_HOME — the relaunched host
# must find ITS ticket, and the joiner must never see one. Prints
# HOSTRESUME-E2E-OK on success. See TESTING.md.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

XDG_A="$OUT/xdg-host"; XDG_B="$OUT/xdg-joiner"
mkdir -p "$XDG_A" "$XDG_B"
launch_with() {  # XDG NAME -> pid
  XDG_DATA_HOME="$1" "$ROOT/newtonia" > "$OUT/$2.log" 2>&1 & echo $!
}
fail() { echo "FAIL: $1"; kill -9 $PA $PB 2>/dev/null; exit 1; }

PA=$(launch_with "$XDG_A" host)
sleep 2
PB=$(launch_with "$XDG_B" joiner)
sleep 4
WINS=$(newtonia_windows)
A=$(echo "$WINS" | head -1); B=$(echo "$WINS" | tail -1)
[ "$A" != "$B" ] || fail "only one window"

nav_host $A
CODE=$(host_room_code host)
[ -n "$CODE" ] || fail "no room code"
echo "room code: $CODE"
nav_join $B "$CODE" joiner
echo "== waiting for connect + play"; sleep 18
alive $PA host; alive $PB joiner

# The first checkpoint lands when the lobby hands the room to the game.
PREF_A="$XDG_A/cc.gfm/newtonia"
[ -f "$PREF_A/netplay_resume.dat" ] || fail "no resume ticket while hosting"
[ -f "$PREF_A/online_savegame.dat" ] || fail "no online save while hosting"

# March one level so the resumed world is distinguishable from a fresh
# game (skip-level checkpoints the online save at the rebuild).
key $A n; sleep 6
grep -aq "Presence: Level 2 Co-Op" "$OUT/host.log" || fail "host never reached level 2"

echo "== SIGKILL the HOST mid-game"
kill -9 $PA; sleep 3
alive $PB joiner
[ -f "$PREF_A/netplay_resume.dat" ] || fail "ticket lost with the process"

echo "== relaunch host, drive RESUME HOSTING $CODE"
PA=$(launch_with "$XDG_A" host2)
sleep 4
for w in $(newtonia_windows); do [ "$w" != "$B" ] && A=$w; done
key $A Return; sleep 1   # attract -> menu; RESUME HOSTING sits selected on top
key $A Return            # confirm it
echo "== waiting for reclaim + client auto-rejoin"; sleep 20
alive $PA host2; alive $PB joiner
grep -aq "net: resuming hosted room $CODE" "$OUT/host2.log" || fail "resume constructor never ran"
grep -aq "room $CODE reclaimed" "$OUT/host2.log" || fail "room never reclaimed"
grep -aq "net: player 2 rejoined" "$OUT/host2.log" || fail "client never rejoined"
grep -aq "Presence: Level 2 Co-Op" "$OUT/host2.log" || fail "generation did not survive the resume"
shot $A hostresume-host; shot $B hostresume-joiner

# The resumed host auto-paused awaiting the rejoin and stays paused
# through it (rejoin.sh semantics): unpause and verify both play on. The
# long paused window before this unpause is also the RX-watchdog
# regression case — a stale input baseline used to kill the session here.
key $A p; sleep 3
alive $PA host2; alive $PB joiner
grep -aq "RX watchdog" "$OUT/host2.log" && fail "spurious RX watchdog after paused resume"

# Quit to menu = deliberate teardown: the room is closed and both resume
# files deleted (a bare app exit deliberately KEEPS them — an accidental
# window close stays resumable like a crash).
key $A Escape; sleep 3
[ ! -f "$PREF_A/netplay_resume.dat" ] || fail "ticket survived quit-to-menu"
[ ! -f "$PREF_A/online_savegame.dat" ] || fail "online save survived quit-to-menu"

kill_pair $PA $PB
assert_clean "$OUT"/host.log "$OUT"/host2.log "$OUT"/joiner.log
echo "HOSTRESUME-E2E-OK"
