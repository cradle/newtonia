#!/bin/bash
# Builds static MbedTLS + libdatachannel for iOS (see NETPLAY.md M3-4).
#
#   ./build_netplay_deps_ios.sh device      # arm64 iphoneos    -> ./netplay-libs-ios
#   ./build_netplay_deps_ios.sh simulator   # arm64+x86_64 sim  -> ./netplay-libs-ios-sim
#
# Then wire the Xcode project (see ios/README.md): add NEWTONIA_NET_RTC=1
# to GCC_PREPROCESSOR_DEFINITIONS, the libdatachannel include dir to
# HEADER_SEARCH_PATHS, and every .a from the prefix to the link phase.
# Pins match every other platform: MbedTLS v3.6.6, libdatachannel v0.24.5.
set -e

KIND="${1:-device}"
case "$KIND" in
  device)    SYSROOT=iphoneos;        ARCHS="arm64";        SUFFIX="ios" ;;
  simulator) SYSROOT=iphonesimulator; ARCHS="arm64;x86_64"; SUFFIX="ios-sim" ;;
  *) echo "usage: $0 [device|simulator]"; exit 1 ;;
esac

ROOT="$PWD"
PREFIX="$PWD/netplay-libs-$SUFFIX"
SRC="$(mktemp -d)"
# The DTLS-SRTP user config, quotes escaped to survive shell->CMake->compiler.
UCFG="-DMBEDTLS_USER_CONFIG_FILE=\\\"$ROOT/xbox/mbedtls_user_config.h\\\""
IOSFLAGS=(-DCMAKE_SYSTEM_NAME=iOS
          -DCMAKE_OSX_SYSROOT=$SYSROOT
          -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0
          "-DCMAKE_OSX_ARCHITECTURES=$ARCHS"
          -DCMAKE_BUILD_TYPE=Release)

echo "== mbedtls v3.6.6 ($KIND)"
git clone --branch v3.6.6 --depth 1 --recurse-submodules --shallow-submodules \
  https://github.com/Mbed-TLS/mbedtls.git "$SRC/mbedtls"
cmake -S "$SRC/mbedtls" -B "$SRC/mbedtls/build" "${IOSFLAGS[@]}" \
  -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF \
  -DCMAKE_C_FLAGS="$UCFG" -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build "$SRC/mbedtls/build" -j"$(sysctl -n hw.logicalcpu)"
cmake --install "$SRC/mbedtls/build"

echo "== libdatachannel v0.24.5 ($KIND)"
git clone --branch v0.24.5 --depth 1 --recurse-submodules --shallow-submodules \
  https://github.com/paullouisageneau/libdatachannel.git "$SRC/libdatachannel"
cmake -S "$SRC/libdatachannel" -B "$SRC/libdatachannel/build" "${IOSFLAGS[@]}" \
  -DBUILD_SHARED_LIBS=OFF \
  -DUSE_MBEDTLS=ON -DNO_MEDIA=ON -DNO_WEBSOCKET=OFF \
  -DNO_EXAMPLES=ON -DNO_TESTS=ON \
  -DCMAKE_PREFIX_PATH="$PREFIX" \
  -DCMAKE_C_FLAGS="$UCFG" -DCMAKE_CXX_FLAGS="$UCFG"
cmake --build "$SRC/libdatachannel/build" -j"$(sysctl -n hw.logicalcpu)" \
  --target datachannel-static

# Gather every static archive (libdatachannel + vendored juice/usrsctp).
find "$SRC/libdatachannel/build" -name "*.a" -exec cp {} "$PREFIX/lib/" \;
cp -r "$SRC/libdatachannel/include/rtc" "$PREFIX/include/"

echo ""
echo "Done. Archives + headers in $PREFIX"
ls "$PREFIX/lib"
