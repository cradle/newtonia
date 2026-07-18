#!/bin/bash
# Builds libdatachannel (the netplay WebRTC library — see NETPLAY.md) from
# source and installs it into a local prefix for `make NETPLAY=1`.
#
#   ./build_netplay_deps.sh              # installs into ./netplay-libs
#   make NETPLAY=1 NETPLAY_PREFIX=$PWD/netplay-libs
#
# macOS universal (arm64 + x86_64, for `make osx NETPLAY=1`):
#   ./build_netplay_deps.sh --universal   # installs into ./netplay-libs
# Universal builds use MbedTLS (built here, fat) instead of Homebrew's
# OpenSSL, which only ships the machine's own architecture.
#
# Requirements: git, cmake (brew install cmake), and OpenSSL headers
# (macOS: brew install openssl@3 — auto-detected below; Linux: libssl-dev).
set -e

UNIVERSAL=0
if [ "${1:-}" = "--universal" ]; then
  UNIVERSAL=1
  shift
fi

ROOT="$PWD"
PREFIX="${1:-$PWD/netplay-libs}"
TAG=v0.24.5           # keep in lockstep with xbox/CMakeLists.txt (see NETPLAY.md)
MBEDTLS_TAG=v3.6.6    # same pin as the Windows FetchContent build

SRC="$(mktemp -d)/libdatachannel"
echo "== cloning libdatachannel $TAG"
git clone --branch "$TAG" --depth 1 --recurse-submodules --shallow-submodules \
  https://github.com/paullouisageneau/libdatachannel.git "$SRC"

# Windows (MSYS2 MINGW64 shell) — mirrors .github/workflows/windows.yml:
# static libdatachannel (+ vendored libjuice/usrsctp) against msys2's
# OpenSSL. No `cmake --install` (it wants the shared target); instead the
# headers and every built archive are copied into the prefix, and the
# Makefile's Windows NETPLAY branch links all lib/*.a in one --start-group.
# Needs: pacman -S git mingw-w64-x86_64-{gcc,cmake,ninja,openssl}
# (setup_windows_build.ps1 installs these).
case "$(uname)" in
MINGW*|MSYS*)
  [ "$UNIVERSAL" = "0" ] || { echo "--universal is macOS-only"; exit 1; }
  echo "== building (Windows static)"
  cmake -S "$SRC" -B "$SRC/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DNO_MEDIA=ON -DNO_WEBSOCKET=OFF -DNO_EXAMPLES=ON -DNO_TESTS=ON
  cmake --build "$SRC/build" --target datachannel-static
  echo "== installing into $PREFIX"
  mkdir -p "$PREFIX/include" "$PREFIX/lib"
  cp -r "$SRC/include/." "$PREFIX/include/"
  find "$SRC/build" -name '*.a' -exec cp {} "$PREFIX/lib/" \;
  echo ""
  echo "Done. Build the game with:"
  echo "  make NETPLAY=1"
  exit 0
  ;;
esac

EXTRA=()
if [ "$UNIVERSAL" = "1" ]; then
  [ "$(uname)" = "Darwin" ] || { echo "--universal is macOS-only"; exit 1; }
  ARCHS="arm64;x86_64"
  # MbedTLS needs the DTLS-SRTP API compiled in for libdatachannel even
  # with NO_MEDIA (the same fix as the Windows build) — the user config
  # header defines MBEDTLS_SSL_DTLS_SRTP for library and consumer alike.
  # The quotes must survive shell -> CMake -> make -> shell -> compiler,
  # so they are backslash-escaped here: the generated compile line then
  # carries \"...\" and the compiler sees a proper "filename" token.
  UCFG="-DMBEDTLS_USER_CONFIG_FILE=\\\"$ROOT/xbox/mbedtls_user_config.h\\\""

  MTLS="$(dirname "$SRC")/mbedtls"
  echo "== cloning mbedtls $MBEDTLS_TAG"
  git clone --branch "$MBEDTLS_TAG" --depth 1 --recurse-submodules --shallow-submodules \
    https://github.com/Mbed-TLS/mbedtls.git "$MTLS"
  echo "== building mbedtls (universal)"
  cmake -B "$MTLS/build" -S "$MTLS" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="$ARCHS" \
    -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF \
    -DCMAKE_C_FLAGS="$UCFG" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX"
  cmake --build "$MTLS/build" -j"$(getconf _NPROCESSORS_ONLN)"
  cmake --install "$MTLS/build"

  EXTRA+=("-DCMAKE_OSX_ARCHITECTURES=$ARCHS"
          "-DUSE_MBEDTLS=ON"
          "-DCMAKE_PREFIX_PATH=$PREFIX"
          "-DCMAKE_C_FLAGS=$UCFG"
          "-DCMAKE_CXX_FLAGS=$UCFG")
else
  # Pin the target arch to the running machine: an Intel-homebrew cmake or a
  # Rosetta terminal otherwise builds an x86_64 dylib on Apple Silicon, and
  # the game link fails with "undefined symbols for architecture arm64".
  if [ "$(uname)" = "Darwin" ]; then
    EXTRA+=("-DCMAKE_OSX_ARCHITECTURES=$(uname -m)")
  fi
  if command -v brew > /dev/null 2>&1; then
    # Homebrew's OpenSSL is keg-only; point CMake at it explicitly.
    if brew --prefix openssl@3 > /dev/null 2>&1; then
      EXTRA+=("-DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3)")
    fi
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
