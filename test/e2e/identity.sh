#!/bin/bash
# Peer-identity happy path, two phases:
#   A) two current builds connect via a room code and each side must log
#      the OTHER's identity from the HELLO/WELCOME append ("net: identity
#      peer name='PLAYER' platform=DESKTOP(1)" — the default backend's
#      generic name + compile-time platform).
#   B) badge-only: the host withholds its display name (name_len 0 via the
#      NEWTONIA_NET_ANON_IDENTITY hook — a valid wire state some platform
#      backends produce deliberately; names are optional). The joiner must
#      log the nameless identity (name='' with the platform still known,
#      NOT the legacy "identity none" line) and connect normally.
# Guards the identity exchange end-to-end (net_identity.*, net_session.cpp).
# Prints IDENTITY-E2E-OK. See TESTING.md.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

# pair HOSTNAME JOINERNAME ANON: one host+joiner run to a connected game;
# logs at $OUT/$1.log / $OUT/$2.log. ANON=1 withholds the host's name.
pair() {
  local hname=$1 jname=$2 anon=$3 pa pb wins a b code
  if [ "$anon" = 1 ]; then
    pa=$(NEWTONIA_NET_ANON_IDENTITY=1 launch "$hname")
  else
    pa=$(launch "$hname")
  fi
  sleep 2
  pb=$(launch "$jname")
  sleep 4

  wins=$(newtonia_windows)
  [ "$(echo "$wins" | wc -l)" -eq 2 ] ||
    { echo "expected 2 game windows, got: $wins"; kill_pair $pa $pb; exit 1; }
  a=$(echo "$wins" | head -1); b=$(echo "$wins" | tail -1)

  nav_host $a
  code=$(host_room_code "$hname")
  [ -n "$code" ] || { echo "NO ROOM CODE"; kill_pair $pa $pb; exit 1; }
  echo "room code ($hname): $code"

  nav_join $b "$code"
  echo "== waiting for connect ($hname vs $jname)"; sleep 18
  alive $pa "$hname"; alive $pb "$jname"
  grep -aq "bootstrap adopted" "$OUT/$jname.log" ||
    { echo "NO BOOTSTRAP ($jname)"; kill_pair $pa $pb; exit 1; }

  kill_pair $pa $pb
  assert_clean "$OUT/$hname.log" "$OUT/$jname.log"
  wait_no_windows
}

echo "=== A: both sides full identity"
pair host joiner 0
# The host learns the client's identity from HELLO; the client learns the
# host's from WELCOME. Both sides of this run are the same desktop build.
grep -aq "net: identity peer name='PLAYER' platform=DESKTOP(1)" "$OUT/host.log" ||
  { echo "IDENTITY-E2E-FAIL: host never logged the client identity"; exit 1; }
grep -aq "net: identity peer name='PLAYER' platform=DESKTOP(1)" "$OUT/joiner.log" ||
  { echo "IDENTITY-E2E-FAIL: joiner never logged the host identity"; exit 1; }

echo "=== B: host withholds its name (badge-only identity)"
pair anon_host anon_joiner 1
# The joiner sees platform-known/name-withheld — distinct from legacy.
grep -aq "net: identity peer name='' platform=DESKTOP(1)" "$OUT/anon_joiner.log" ||
  { echo "IDENTITY-E2E-FAIL: joiner never logged the badge-only identity"; exit 1; }
grep -aq "net: identity none" "$OUT/anon_joiner.log" &&
  { echo "IDENTITY-E2E-FAIL: badge-only host mistaken for a legacy peer"; exit 1; }
# The joiner's own identity is unaffected by the host's hook.
grep -aq "net: identity peer name='PLAYER' platform=DESKTOP(1)" "$OUT/anon_host.log" ||
  { echo "IDENTITY-E2E-FAIL: anon host never logged the client identity"; exit 1; }

echo "IDENTITY-E2E-OK"
