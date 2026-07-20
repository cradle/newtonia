#!/bin/bash
# Mixed-version interop guard for the identity append (the reverse-read
# trap: parsing appended HELLO/WELCOME fields from an OLD peer's short
# message must not fail the handshake — see net_protocol.h). Both
# directions, via the NEWTONIA_NET_NO_IDENTITY hook that makes a current
# build send the pre-identity (short) messages:
#   A) legacy HOST (short WELCOME) vs current joiner
#   B) current host vs legacy JOINER (short HELLO)
# Each pairing must fully connect (joiner bootstraps) and the current side
# must land in the no-badge path ("net: identity none (legacy peer)");
# the legacy side accepting the LONG message (append ignored as trailing
# bytes) is proven by the bootstrap itself. Prints IDENTITY-LEGACY-E2E-OK.
# See TESTING.md.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

# wait_no_windows: poll until every game window from the previous pairing
# is gone — the second pairing otherwise scoops a lingering stale window id
# and sends its keys to a corpse.
wait_no_windows() {
  local i
  for i in $(seq 1 15); do
    [ -z "$(newtonia_windows)" ] && return 0
    sleep 1
  done
  echo "stale game windows never closed"; exit 1
}

# run_pair HOSTNAME JOINNAME LEGACYSIDE(host|joiner): one full room-code
# connect; the legacy side launches with NEWTONIA_NET_NO_IDENTITY=1 (the
# env stays contained — command substitution runs in a subshell).
run_pair() {
  local hname=$1 jname=$2 legacy=$3 pa pb wins a b code c
  wait_no_windows
  if [ "$legacy" = host ]; then
    pa=$(NEWTONIA_NET_NO_IDENTITY=1 launch "$hname")
  else
    pa=$(launch "$hname")
  fi
  sleep 2
  if [ "$legacy" = joiner ]; then
    pb=$(NEWTONIA_NET_NO_IDENTITY=1 launch "$jname")
  else
    pb=$(launch "$jname")
  fi
  sleep 4

  wins=$(newtonia_windows)
  [ "$(echo "$wins" | wc -l)" -eq 2 ] ||
    { echo "expected 2 game windows, got: $wins"; kill $pa $pb; exit 1; }
  a=$(echo "$wins" | head -1); b=$(echo "$wins" | tail -1)
  [ "$a" != "$b" ] || { echo "only one window"; exit 1; }

  # Host: attract -> menu -> ONLINE -> HOST
  key $a Return; sleep 1; key $a s; key $a Return; sleep 1; key $a Return
  code=$(host_room_code "$hname")
  [ -n "$code" ] || { echo "NO ROOM CODE ($hname)"; kill $pa $pb; exit 1; }
  echo "room code ($hname): $code"

  # Joiner: attract -> menu -> ONLINE -> JOIN -> type the code
  key $b Return; sleep 1; key $b s; key $b Return; sleep 1; key $b s; key $b Return; sleep 1
  for c in $(echo "$code" | grep -o .); do key $b "$c"; done
  echo "== waiting for connect ($hname vs $jname)"; sleep 18
  alive $pa "$hname"; alive $pb "$jname"
  grep -aq "bootstrap adopted" "$OUT/$jname.log" ||
    { echo "NO BOOTSTRAP ($jname)"; exit 1; }
  # SIGTERM (clean quit via the SDL_QUIT path) with a hard-kill fallback:
  # a wedged instance's stale windows would otherwise eat the next
  # pairing's keys. The logs are already written; SIGKILL loses nothing.
  kill $pa $pb 2>/dev/null; sleep 2
  kill -9 $pa $pb 2>/dev/null; wait $pa $pb 2>/dev/null
  assert_clean "$OUT/$hname.log" "$OUT/$jname.log"
  wait_no_windows  # the next pairing must start from a clean window list
}

echo "=== A: legacy host (short WELCOME) vs current joiner"
run_pair old_host new_joiner host
grep -aq "net: identity none (legacy peer)" "$OUT/new_joiner.log" ||
  { echo "IDENTITY-LEGACY-E2E-FAIL: joiner did not take the legacy path"; exit 1; }

echo "=== B: current host vs legacy joiner (short HELLO)"
run_pair new_host old_joiner joiner
grep -aq "net: identity none (legacy peer)" "$OUT/new_host.log" ||
  { echo "IDENTITY-LEGACY-E2E-FAIL: host did not take the legacy path"; exit 1; }

echo "IDENTITY-LEGACY-E2E-OK"
