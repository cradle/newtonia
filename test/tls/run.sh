#!/usr/bin/env bash
# Build + run the WebSocket TLS verification gate (LEADERBOARD.md S1).
#
#   ./test/tls/run.sh [netplay-prefix]      # default ./netplay-libs
#
# Needs the same libdatachannel prefix the game links against (built by
# build_netplay_deps.sh, WITH patches/libdatachannel-ws-ca-cert.patch) plus
# openssl(1) to mint the throwaway certificates. Exits non-zero if any of the
# three cases in verify_test.cc misbehaves.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PREFIX="${1:-$ROOT/netplay-libs}"
if [ ! -f "$PREFIX/include/rtc/rtc.h" ]; then
  echo "tls: no libdatachannel at $PREFIX - run ./build_netplay_deps.sh first" >&2
  exit 2
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "tls: minting throwaway certificates in $WORK"
openssl req -x509 -newkey rsa:2048 -nodes -days 1 \
  -keyout "$WORK/ca.key" -out "$WORK/ca.pem" \
  -subj "/CN=Newtonia Test CA" 2>/dev/null
openssl req -newkey rsa:2048 -nodes \
  -keyout "$WORK/srv.key" -out "$WORK/srv.csr" \
  -subj "/CN=localhost" 2>/dev/null
printf "subjectAltName=DNS:localhost\n" > "$WORK/san.cnf"
openssl x509 -req -in "$WORK/srv.csr" -CA "$WORK/ca.pem" -CAkey "$WORK/ca.key" \
  -CAcreateserial -out "$WORK/srv.crt" -days 1 -extfile "$WORK/san.cnf" 2>/dev/null
# A second, unrelated CA: the stand-in for an attacker's certificate.
openssl req -x509 -newkey rsa:2048 -nodes -days 1 \
  -keyout "$WORK/other.key" -out "$WORK/other-ca.pem" \
  -subj "/CN=Unrelated CA" 2>/dev/null

echo "tls: building the gate"
LIBS=""
if [ -n "$(ls "$PREFIX"/lib/*.a 2>/dev/null || true)" ]; then
  # Static prefix (Windows-style): the archives need OpenSSL spelled out,
  # which a shared build carries in its own DT_NEEDED.
  LIBS="-Wl,--start-group $PREFIX/lib/*.a -Wl,--end-group -lssl -lcrypto"
else
  LIBS="-L$PREFIX/lib -ldatachannel -Wl,-rpath,$PREFIX/lib"
fi
# shellcheck disable=SC2086
g++ -std=c++17 -O1 -I"$PREFIX/include" -o "$WORK/verify_test" \
  "$ROOT/test/tls/verify_test.cc" $LIBS -lpthread

echo "tls: running"
"$WORK/verify_test" "$WORK"
