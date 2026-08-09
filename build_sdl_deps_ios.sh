#!/bin/bash
# Builds static SDL2 + SDL2_mixer for iOS — the third of the three
# one-time steps behind `make ios` (see the Makefile's iOS section).
#
#   ./build_sdl_deps_ios.sh device      # arm64 iphoneos    -> ./sdl-libs-ios
#   ./build_sdl_deps_ios.sh simulator   # arm64+x86_64 sim  -> ./sdl-libs-ios-sim
#
# Mirrors the deploy-ios.yml / ios.yml recipes exactly (same pins, same
# cmake flags): SDL release-2.32.10, SDL_mixer release-2.8.2, WAV-only
# codec set. Layout: $PREFIX/lib/*.a + $PREFIX/include/SDL2/*.h (the
# dispatch SDL_config.h from the source tree, proven by CI — no
# build-generated config needed).
set -e

KIND="${1:-device}"
case "$KIND" in
  device)    SYSROOT=iphoneos;        ARCHS="arm64";        SUFFIX="ios" ;;
  simulator) SYSROOT=iphonesimulator; ARCHS="arm64;x86_64"; SUFFIX="ios-sim" ;;
  *) echo "usage: $0 [device|simulator]"; exit 1 ;;
esac

PREFIX="$PWD/sdl-libs-$SUFFIX"
SRC="$(mktemp -d)"
IOSFLAGS=(-DCMAKE_SYSTEM_NAME=iOS
          -DCMAKE_OSX_SYSROOT=$SYSROOT
          -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
          "-DCMAKE_OSX_ARCHITECTURES=$ARCHS"
          -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY=""
          -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED=NO
          -DCMAKE_C_FLAGS=-DGLES_SILENCE_DEPRECATION
          -DCMAKE_CXX_FLAGS=-DGLES_SILENCE_DEPRECATION)

echo "== SDL2 release-2.32.10 ($KIND)"
git clone --depth 1 --branch release-2.32.10 \
  https://github.com/libsdl-org/SDL.git "$SRC/SDL2"
cmake -S "$SRC/SDL2" -B "$SRC/SDL2/build" "${IOSFLAGS[@]}" \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_HIDAPI=OFF
cmake --build "$SRC/SDL2/build" --config Release -j"$(sysctl -n hw.logicalcpu)"

echo "== SDL2_mixer release-2.8.2 ($KIND)"
git clone --depth 1 --branch release-2.8.2 \
  https://github.com/libsdl-org/SDL_mixer.git "$SRC/SDL2_mixer"
git -C "$SRC/SDL2_mixer" submodule update --init --depth 1
SDL2_LIB=$(find "$SRC/SDL2/build" -name "libSDL2.a" | head -1)
cmake -S "$SRC/SDL2_mixer" -B "$SRC/SDL2_mixer/build" "${IOSFLAGS[@]}" \
  -DSDL_SHARED=OFF \
  -DSDL2MIXER_SHARED=OFF \
  -DBUILD_SHARED_LIBS=OFF \
  -DSDL2MIXER_SAMPLES=OFF \
  -DSDL2MIXER_VORBIS="" \
  -DSDL2MIXER_WAVPACK=OFF \
  -DSDL2MIXER_FLAC=OFF \
  -DSDL2MIXER_OPUS=OFF \
  -DSDL2MIXER_MOD=OFF \
  -DSDL2MIXER_MIDI=OFF \
  -DSDL2MIXER_MP3=OFF \
  -DSDL2_LIBRARY="$SDL2_LIB" \
  -DSDL2_INCLUDE_DIR="$SRC/SDL2/include"
cmake --build "$SRC/SDL2_mixer/build" --config Release -j"$(sysctl -n hw.logicalcpu)"

mkdir -p "$PREFIX/lib" "$PREFIX/include/SDL2"
find "$SRC/SDL2/build" "$SRC/SDL2_mixer/build" -name "*.a" \
  -exec cp {} "$PREFIX/lib/" \;
cp "$SRC/SDL2/include/"*.h "$PREFIX/include/SDL2/"
cp "$SRC/SDL2_mixer/include/"*.h "$PREFIX/include/SDL2/"

echo ""
echo "Done. Archives + headers in $PREFIX"
ls "$PREFIX/lib"
