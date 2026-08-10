#!/bin/bash
# B5 smoke: per-seat rejoin with play-on. A 3-seat room auto-starts, seat
# 3's client is SIGKILLed mid-game — the host must mark PLAYER 3 lost and
# keep playing UNPAUSED (PB-D7), park only that hull, and rejoin a
# relaunched client onto seat 3. Since B6 this is a SEATS=3 run of the
# generalized N-seat driver; the wrapper (and its THREESEAT-REJOIN-OK
# line) stays for the docs/grep contract.
set -u
SEATS=3 "$(dirname "$0")/nseat_rejoin.sh" "$@" || exit 1
echo "THREESEAT-REJOIN-OK"
