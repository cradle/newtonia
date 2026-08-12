#!/bin/bash
# Mixed-version interop guard for the identity append (the reverse-read
# trap: parsing appended HELLO/WELCOME fields from an OLD peer's short
# message must not fail the handshake — see net_protocol.h). Both
# directions, via the NEWTONIA_NET_NO_IDENTITY hook that makes a current
# build send the pre-identity (short) messages:
#   A) legacy HOST (short WELCOME) vs current joiner
#   B) current host vs legacy JOINER (short HELLO)
# Each pairing must fully connect (joiner bootstraps) and the current side
# must land in the no-badge path ("net: identity none (legacy peer)").
# Note the hook only suppresses the SEND — the "legacy" side still runs the
# current reader, so the long-message direction rests on the fixed-field
# accept logic being identical to the old builds' (it is: the identity
# parse is strictly after the old accept checks) rather than on a real old
# binary. Prints IDENTITY-LEGACY-E2E-OK. See TESTING.md.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

# run_pair HOSTNAME JOINNAME LEGACYSIDE(host|joiner): one full room-code
# connect; the legacy side launches with NEWTONIA_NET_NO_IDENTITY=1 (the
# env stays contained — command substitution runs in a subshell).
run_pair() {
  local hname=$1 jname=$2 legacy=$3 pa pb before a b code
  wait_no_windows
  # The previous pairing hosted and was killed, leaving a resume ticket whose
  # "RESUME HOSTING <CODE>" row sits above the layout nav_host assumes — the
  # phase-B "NO ROOM CODE" (lib.sh).
  fresh_menu_state
  # Per-launch window diff, not a sorted id list: across sequential pairings X
  # recycles the previous windows' ids, so "head -1" is not reliably the older
  # instance (the hazard new_window_since was added for, B5).
  before=$(newtonia_windows)
  if [ "$legacy" = host ]; then
    pa=$(NEWTONIA_NET_NO_IDENTITY=1 launch "$hname")
  else
    pa=$(launch "$hname")
  fi
  sleep 2
  a=$(new_window_since "$before")
  before=$(newtonia_windows)
  if [ "$legacy" = joiner ]; then
    pb=$(NEWTONIA_NET_NO_IDENTITY=1 launch "$jname")
  else
    pb=$(launch "$jname")
  fi
  sleep 4
  b=$(new_window_since "$before")

  [ -n "$a" ] && [ -n "$b" ] && [ "$a" != "$b" ] ||
    { echo "expected 2 game windows, got: '$a' '$b'"; kill_pair $pa $pb; exit 1; }

  # On the menu before typing: the second pairing starts slower and a
  # swallowed first Return shifts the whole nav sequence (lib.sh).
  wait_for_menu "$hname" || { kill_pair $pa $pb; exit 1; }
  wait_for_menu "$jname" || { kill_pair $pa $pb; exit 1; }

  nav_host $a
  code=$(host_room_code "$hname")
  [ -n "$code" ] || { echo "NO ROOM CODE ($hname)"; kill_pair $pa $pb; exit 1; }
  echo "room code ($hname): $code"

  nav_join $b "$code"
  echo "== waiting for connect ($hname vs $jname)"; sleep 18
  alive $pa "$hname"; alive $pb "$jname"
  grep -aq "bootstrap adopted" "$OUT/$jname.log" ||
    { echo "NO BOOTSTRAP ($jname)"; exit 1; }
  kill_pair $pa $pb
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
