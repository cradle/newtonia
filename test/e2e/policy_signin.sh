#!/bin/bash
# The lobby's sign-in refusal (net_policy.h NET_ONLINE_REFUSED_SIGN_IN): a
# build whose platform has nobody signed in (NEWTONIA_NET_TEST_ONLINE_SIGNIN=
# signin:N, the default backend's inert hook: refused as SIGN_IN until N ms
# after the first explain(), i.e. the sign-in dialog opening and a player
# signing in) must say SIGN IN rather than the catch-all "not allowed", ask
# the platform for its sign-in at the HOST press, and go back to the
# HOST/JOIN chooser by itself once the sign-in lands, where HOST then opens
# a room. Prints POLICY-SIGNIN-E2E-OK. See TESTING.md.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
relay_check

PA=$(launch host NEWTONIA_NET_TEST_ONLINE_SIGNIN=signin:4000)
sleep 3
A=$(newtonia_windows | head -1)
[ -n "$A" ] || { echo "no game window"; kill $PA 2>/dev/null; exit 1; }

# nav_host's keys: attract, ONLINE, HOST (refused here).
key $A Return; sleep 1; key $A s; key $A Return; sleep 1; key $A Return
sleep 1
alive $PA host
shot $A policy-signin-refused
grep -aq "\[lobby\] online play refused (reason 1)" "$OUT/host.log" ||
  { echo "POLICY-SIGNIN-E2E-FAIL: no sign-in refusal"; kill $PA; exit 1; }
grep -aq "net: platform sign-in dialog opened" "$OUT/host.log" ||
  { echo "POLICY-SIGNIN-E2E-FAIL: sign-in never asked for"; kill $PA; exit 1; }

echo "== waiting for the sign-in to land"
for i in $(seq 1 20); do
  grep -aq "online play allowed again - back to the chooser" "$OUT/host.log" &&
    break
  sleep 0.5
done
grep -aq "online play allowed again - back to the chooser" "$OUT/host.log" ||
  { echo "POLICY-SIGNIN-E2E-FAIL: chooser never came back"; kill $PA; exit 1; }
shot $A policy-signin-chooser

# The chooser is back on HOST: one press opens a room now.
key $A Return
CODE=$(host_room_code host)
alive $PA host
kill $PA 2>/dev/null; wait $PA 2>/dev/null
assert_clean "$OUT/host.log"
[ -n "$CODE" ] ||
  { echo "POLICY-SIGNIN-E2E-FAIL: HOST after sign-in opened no room"; exit 1; }
echo "room code after sign-in: $CODE"
echo "POLICY-SIGNIN-E2E-OK"
