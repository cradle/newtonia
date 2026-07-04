#!/bin/bash
# Builds libdatachannel (the netplay WebRTC library — see NETPLAY.md) from
# source and installs it into a local prefix for `make NETPLAY=1`.
#
#   ./build_netplay_deps.sh              # installs into ./netplay-libs
#   make NETPLAY=1 NETPLAY_PREFIX=$PWD/netplay-libs
#
# Requirements: git, cmake (brew install cmake), and OpenSSL headers
# (macOS: brew install openssl@3 — auto-detected below; Linux: libssl-dev).
set -e

PREFIX="${1:-$PWD/netplay-libs}"
TAG=v0.24.5   # keep in lockstep with xbox/CMakeLists.txt (see NETPLAY.md)

SRC="$(mktemp -d)/libdatachannel"
echo "== cloning libdatachannel $TAG"
git clone --branch "$TAG" --depth 1 --recurse-submodules --shallow-submodules \
  https://github.com/paullouisageneau/libdatachannel.git "$SRC"

EXTRA=()
if command -v brew > /dev/null 2>&1; then
  # Homebrew's OpenSSL is keg-only; point CMake at it explicitly.
  if brew --prefix openssl@3 > /dev/null 2>&1; then
    EXTRA+=("-DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3)")
  fi
fi

echo "== building"
cmake -B "$SRC/build" -S "$SRC" \
  -DCMAKE_BUILD_TYPE=Release \
  -DNO_MEDIA=ON -DNO_WEBSOCKET=OFF -DNO_EXAMPLES=ON -DNO_TESTS=ON \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  "${EXTRA[@]}"
cmake --build "$SRC/build" -j"$(getconf _NPROCESSORS_ONLN)"

echo "== installing into $PREFIX"
cmake --install "$SRC/build"

echo ""
echo "Done. Build the game with:"
echo "  make clean && make NETPLAY=1 NETPLAY_PREFIX=$PREFIX"
