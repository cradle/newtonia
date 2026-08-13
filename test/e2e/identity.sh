#!/bin/bash
# Peer-identity happy path, assertions across three pairings:
#   A) named exchange: both sides carry a display name (the
#      NEWTONIA_NET_NAME dev hook — desktop default builds send no name)
#      and each side must log the OTHER's from the HELLO/WELCOME append
#      ("net: identity peer name='GLENN' platform=DESKTOP(1)").
#   B) badge-only: the host withholds its display name (name_len 0 via
#      NEWTONIA_NET_ANON_IDENTITY=1, overriding its configured name — a
#      valid wire state some platform backends produce deliberately;
#      names are optional). The joiner must log the nameless identity
#      (name='' with the platform still known, NOT the legacy "identity
#      none" line) and connect normally; the receiver renders role labels
#      (PLAYER 1 = host, PLAYER 2 = client) in name-bearing text.
#   C) accented names: a name with Latin accents (BJÖRN, RENÉE) must fold
#      to its Typer-drawable ASCII base (BJORN, RENEE) rather than dropping
#      the accented letters — Tier-1 transliteration in net_sanitize_name.
# Guards the identity exchange end-to-end (net_identity.*, net_session.cpp).
# Prints IDENTITY-E2E-OK. See TESTING.md.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

# pair HOSTNAME JOINERNAME HOSTANON HOSTDISPLAY JOINERDISPLAY: one
# host+joiner run to a connected game; logs at $OUT/$1.log / $OUT/$2.log.
# HOSTANON=1 withholds the host's name on the wire.
pair() {
  local hname=$1 jname=$2 anon=$3 hn=$4 jn=$5 pa pb before a b code
  # The previous pairing hosted and was killed, leaving a resume ticket: its
  # "RESUME HOSTING <CODE>" row would push every row below it down one and
  # nav_host would land on NEW GAME ("NO ROOM CODE" — see lib.sh).
  fresh_menu_state
  # Identify each instance by the window that appeared when IT launched rather
  # than by sorting the id list: across sequential pairings X recycles the
  # previous windows' ids, so "head -1" is not reliably the older instance
  # (the hazard new_window_since was added for, B5).
  before=$(newtonia_windows)
  pa=$(NEWTONIA_NET_NAME="$hn" NEWTONIA_NET_ANON_IDENTITY="$anon" \
       launch "$hname")
  sleep 2
  a=$(new_window_since "$before")
  before=$(newtonia_windows)
  pb=$(NEWTONIA_NET_NAME="$jn" launch "$jname")
  sleep 4
  b=$(new_window_since "$before")

  [ -n "$a" ] && [ -n "$b" ] && [ "$a" != "$b" ] ||
    { echo "expected 2 game windows, got: '$a' '$b'"; kill_pair $pa $pb; exit 1; }

  # Both instances must be ON the menu before anything is typed at them: a
  # later pairing starts slower, and a swallowed first Return shifts the whole
  # sequence (lib.sh).
  wait_for_menu "$hname" || { kill_pair $pa $pb; exit 1; }
  wait_for_menu "$jname" || { kill_pair $pa $pb; exit 1; }

  nav_host $a
  code=$(host_room_code "$hname")
  [ -n "$code" ] || { echo "NO ROOM CODE"; kill_pair $pa $pb; exit 1; }
  echo "room code ($hname): $code"

  nav_join $b "$code" "$jname"
  echo "== waiting for connect ($hname vs $jname)"; sleep 18
  alive $pa "$hname"; alive $pb "$jname"
  grep -aq "bootstrap adopted" "$OUT/$jname.log" ||
    { echo "NO BOOTSTRAP ($jname)"; kill_pair $pa $pb; exit 1; }

  kill_pair $pa $pb
  assert_clean "$OUT/$hname.log" "$OUT/$jname.log"
  wait_no_windows
}

echo "=== A: named identities both ways"
# The host's configured name carries an embedded ESC byte: the sanitizer's
# explicit control-byte strip (net_sanitize_name — a security boundary
# independent of the glyph set) must reduce it to GLENN before it reaches
# the wire; the receive path runs the same function on whatever arrives.
pair host joiner 0 $'GL\x1bENN' BOB
# The host learns the client's identity from HELLO; the client learns the
# host's from WELCOME.
grep -aq "net: identity peer name='BOB' platform=DESKTOP(1)" "$OUT/host.log" ||
  { echo "IDENTITY-E2E-FAIL: host never logged the client identity"; exit 1; }
grep -aq "net: identity peer name='GLENN' platform=DESKTOP(1)" "$OUT/joiner.log" ||
  { echo "IDENTITY-E2E-FAIL: joiner never logged the host identity"; exit 1; }
# Unattested claim (net_identity.h): on an online (worker) session a claimed
# identity must never render — the joiner's greeting uses the role label, not
# GLENN. (This relay is PLAIN — no FAKE_VERIFY — so nothing is attested;
# identity_attested.sh covers the attested path.)
grep -aq "net: banner 'JOINED PLAYER 1 SERVER'" "$OUT/joiner.log" ||
  { echo "IDENTITY-E2E-FAIL: joiner greeting missing the role label"; exit 1; }
grep -aq "net: banner 'JOINED GLENN SERVER'" "$OUT/joiner.log" &&
  { echo "IDENTITY-E2E-FAIL: unverified claimed name reached the display"; exit 1; }

echo "=== B: host withholds its name (badge-only identity)"
pair anon_host anon_joiner 1 GLENN BOB
# The joiner sees platform-known/name-withheld — distinct from legacy —
# even though the host had a name configured.
grep -aq "net: identity peer name='' platform=DESKTOP(1)" "$OUT/anon_joiner.log" ||
  { echo "IDENTITY-E2E-FAIL: joiner never logged the badge-only identity"; exit 1; }
grep -aq "net: identity none" "$OUT/anon_joiner.log" &&
  { echo "IDENTITY-E2E-FAIL: badge-only host mistaken for a legacy peer"; exit 1; }
# The nameless host renders under its role label on the joiner's side
# ("JOINED PLAYER 1 SERVER" greeting banner).
grep -aq "net: banner 'JOINED PLAYER 1 SERVER'" "$OUT/anon_joiner.log" ||
  { echo "IDENTITY-E2E-FAIL: joiner never used the PLAYER 1 role label"; exit 1; }
# The joiner's own identity is unaffected by the host's hook.
grep -aq "net: identity peer name='BOB' platform=DESKTOP(1)" "$OUT/anon_host.log" ||
  { echo "IDENTITY-E2E-FAIL: anon host never logged the client identity"; exit 1; }

echo "=== C: accented names fold to ASCII on the wire (Tier-1 transliteration)"
# net_sanitize_name decodes UTF-8 and folds Latin scripts to their Typer-
# drawable base (BJÖRN -> BJORN, RENÉE -> RENEE) instead of dropping the
# accented letters. The fold runs on send (net_identity.cpp) and again on
# receive (net_session.cpp); the logged peer name proves the whole path.
# Bytes: Ö = C3 96, É = C3 89.
pair accent_host accent_joiner 0 $'BJ\xc3\x96RN' $'REN\xc3\x89E'
grep -aq "net: identity peer name='RENEE' platform=DESKTOP(1)" "$OUT/accent_host.log" ||
  { echo "IDENTITY-E2E-FAIL: accented client name did not fold to RENEE"; exit 1; }
grep -aq "net: identity peer name='BJORN' platform=DESKTOP(1)" "$OUT/accent_joiner.log" ||
  { echo "IDENTITY-E2E-FAIL: accented host name did not fold to BJORN"; exit 1; }

echo "IDENTITY-E2E-OK"
