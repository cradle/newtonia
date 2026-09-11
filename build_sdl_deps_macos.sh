#!/bin/bash
# Pinned SDL3 + sdl2-compat + SDL2_mixer for make osx and both macOS CI jobs.
# Usage: ./build_sdl_deps_macos.sh [arm64-prefix [x86_64-prefix]]
# Requires Xcode Command Line Tools and CMake. Only installation may use sudo.
set -euo pipefail

if [ "$#" -gt 2 ]; then
  echo "usage: $0 [arm64-prefix [x86_64-prefix]]" >&2
  exit 1
fi
if [ "$(uname -s)" != Darwin ]; then
  echo "error: macOS and the Xcode Command Line Tools are required" >&2
  exit 1
fi
SDK=$(xcrun --sdk macosx --show-sdk-path)
CC=$(xcrun --find clang)
CXX=$(xcrun --find clang++)
CMAKE=$(command -v cmake)
JOBS=$(sysctl -n hw.logicalcpu)
ARM_PREFIX=${1:-/opt/homebrew}
X86_PREFIX=${2:-/usr/local}
for prefix in "$ARM_PREFIX" "$X86_PREFIX"; do
  case "$prefix" in
    /*) ;;
    *) echo "error: install prefixes must be absolute paths" >&2; exit 1 ;;
  esac
done
if [ "$ARM_PREFIX" = "$X86_PREFIX" ]; then
  echo "error: each architecture needs a separate install prefix" >&2
  exit 1
fi

SDL3_VER=3.4.16
SDL2_COMPAT_VER=2.32.72
SDL_MIXER_VER=2.8.2
# Keep third-party sources outside the checkout: CI collects *.cpp recursively.
SRC=$(mktemp -d)
trap 'rm -rf "$SRC"' EXIT
fetch() {
  local repo="$1" version="$2" directory="$3"
  # PR #490: the release CDN once failed through the old 5x10s budget.
  curl -fSL --retry 8 --retry-delay 15 --retry-all-errors \
    -o "$SRC/$directory.tar.gz" \
    "https://github.com/libsdl-org/$repo/releases/download/release-$version/$directory.tar.gz"
  tar xzf "$SRC/$directory.tar.gz" -C "$SRC"
}
fetch SDL "$SDL3_VER" "SDL3-$SDL3_VER"
fetch sdl2-compat "$SDL2_COMPAT_VER" "sdl2-compat-$SDL2_COMPAT_VER"
fetch SDL_mixer "$SDL_MIXER_VER" "SDL2_mixer-$SDL_MIXER_VER"

for arch in arm64 x86_64; do
  if [ "$arch" = arm64 ]; then prefix="$ARM_PREFIX"; else prefix="$X86_PREFIX"; fi
  flags=(
    -DCMAKE_BUILD_TYPE=Release
    "-DCMAKE_C_COMPILER=$CC" "-DCMAKE_CXX_COMPILER=$CXX"
    "-DCMAKE_OBJC_COMPILER=$CC"
    "-DCMAKE_OSX_SYSROOT=$SDK" "-DCMAKE_OSX_ARCHITECTURES=$arch"
    -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
    "-DCMAKE_C_FLAGS=-target $arch-apple-macos12.0"
    "-DCMAKE_CXX_FLAGS=-target $arch-apple-macos12.0"
    "-DCMAKE_OBJC_FLAGS=-target $arch-apple-macos12.0"
    "-DCMAKE_SHARED_LINKER_FLAGS=-target $arch-apple-macos12.0"
    "-DCMAKE_PREFIX_PATH=$prefix" "-DCMAKE_INSTALL_PREFIX=$prefix"
    "-DCMAKE_INSTALL_NAME_DIR=$prefix/lib"
    -DCMAKE_INSTALL_LIBDIR=lib
  )
  install_build() {
    "$CMAKE" --build "$1" --parallel "$JOBS"
    if [ -w "$prefix" ]; then
      "$CMAKE" --install "$1"
    else
      sudo "$CMAKE" --install "$1"
    fi
  }

  "$CMAKE" -S "$SRC/SDL3-$SDL3_VER" -B "$SRC/sdl3-$arch" "${flags[@]}" \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_FRAMEWORK=OFF \
    -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
  install_build "$SRC/sdl3-$arch"

  "$CMAKE" -S "$SRC/sdl2-compat-$SDL2_COMPAT_VER" -B "$SRC/sdl2-$arch" "${flags[@]}" \
    "-DSDL3_DIR=$prefix/lib/cmake/SDL3" -DSDL2COMPAT_TESTS=OFF
  install_build "$SRC/sdl2-$arch"

  "$CMAKE" -S "$SRC/SDL2_mixer-$SDL_MIXER_VER" -B "$SRC/mixer-$arch" "${flags[@]}" \
    "-DSDL2_DIR=$prefix/lib/cmake/SDL2" \
    -DBUILD_SHARED_LIBS=ON -DSDL2MIXER_VENDORED=OFF -DSDL2MIXER_SAMPLES=OFF \
    -DSDL2MIXER_VORBIS= -DSDL2MIXER_FLAC=OFF -DSDL2MIXER_MIDI=OFF \
    -DSDL2MIXER_OPUS=OFF -DSDL2MIXER_MOD=OFF -DSDL2MIXER_GME=OFF \
    -DSDL2MIXER_WAVPACK=OFF -DSDL2MIXER_WAVE=ON -DSDL2MIXER_MP3=ON \
    -DSDL2MIXER_MP3_MINIMP3=ON -DSDL2MIXER_MP3_MPG123=OFF
  install_build "$SRC/mixer-$arch"

  for lib in libSDL3.dylib libSDL2.dylib libSDL2_mixer.dylib; do
    lipo -info "$prefix/lib/$lib"
    lipo -verify_arch "$arch" "$prefix/lib/$lib"
    # The recursive bundle walker needs absolute, prefix-paired install names.
    otool -D "$prefix/lib/$lib" | grep -F "$prefix/lib/"
  done
done
