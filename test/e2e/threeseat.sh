#!/bin/bash
# B4b smoke: NEWTONIA_NET_TEST_SEATS=3 waiting room — host + 2 joiners
# through the local relay; the room auto-starts when seat 3 fills. Since
# B6 this is a SEATS=3 run of the generalized N-seat driver; the wrapper
# (and its THREESEAT-SMOKE-OK line) stays for the docs/grep contract.
set -u
SEATS=3 "$(dirname "$0")/nseat.sh" "$@" || exit 1
echo "THREESEAT-SMOKE-OK"
