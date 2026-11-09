#!/usr/bin/env bash
# Builds static SDL2 + SDL2_mixer from pinned source for the Windows
# (MSYS2 MINGW64) builds, and installs them into the MINGW64 prefix so
# `pkg-config sdl2 SDL2_mixer` and `sdl2-config` find them exactly where the
# old pacman packages put them.
#
#   ./build_sdl_deps_mingw.sh            # installs into /mingw64
#   ./build_sdl_deps_mingw.sh <prefix>   # anywhere else
#
# Why not pacman: on 2026-11-09 MSYS2's mingw-w64-x86_64-SDL2_mixer started
# depending on sdl2-compat, which conflicts with (and so displaces) the real
# SDL2 package. sdl2-compat is a shim that loads SDL3 at run time and ships no
# static libSDL2.a, so the fully static newtonia.exe stopped linking
# ("cannot find -lSDL2") on windows.yml and on deploy-steam's Windows job.
#
# The pins and the mixer's codec set (WAV + MP3, every other decoder off)
# match deploy-steam's Linux job, so all three Steam depots run one SDL.
# Callers must NOT also install the pacman SDL2 / SDL2_mixer / sdl2-compat
# packages: their headers and .pc files would shadow these.
set -euo pipefail

SDL2_TAG=release-2.32.10
SDL2_MIXER_TAG=release-2.8.2

PREFIX=${1:-/mingw64}
# CMake is a native Windows program: hand it a mixed-style path.
if command -v cygpath >/dev/null 2>&1; then PREFIX_NATIVE=$(cygpath -m "$PREFIX"); else PREFIX_NATIVE=$PREFIX; fi

if [ -f "$PREFIX/lib/libSDL2.a" ] && [ -f "$PREFIX/lib/libSDL2_mixer.a" ] \
   && grep -qx 'Version: 2.32.10' "$PREFIX/lib/pkgconfig/sdl2.pc" 2>/dev/null; then
  echo "static SDL2 2.32.10 + SDL2_mixer already in $PREFIX; delete $PREFIX/lib/libSDL2.a to force a rebuild"
  exit 0
fi

# Outside the repo: the game's build globs every *.cpp under it.
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# Retry with backoff, wiping the partial clone (GitHub 5xx waves, 2026-08-03
# and 2026-11-08).
try() { local i; for i in 1 2 3; do
          if "$@"; then return 0; fi
          if [ "$i" = 3 ]; then return 1; fi
          echo "attempt $i failed; retrying in $((i*20))s"; sleep $((i*20))
          rm -rf "${CLEAN:?}"; done; }

CLEAN="$WORK/SDL" try git clone --depth 1 --branch "$SDL2_TAG" \
  https://github.com/libsdl-org/SDL.git "$WORK/SDL"
cmake -S "$WORK/SDL" -B "$WORK/SDL/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX_NATIVE" \
  -DSDL_SHARED=OFF \
  -DSDL_STATIC=ON \
  -DSDL_TESTS=OFF
cmake --build "$WORK/SDL/build"
cmake --install "$WORK/SDL/build"

CLEAN="$WORK/SDL_mixer" try git clone --depth 1 --branch "$SDL2_MIXER_TAG" \
  https://github.com/libsdl-org/SDL_mixer.git "$WORK/SDL_mixer"
cmake -S "$WORK/SDL_mixer" -B "$WORK/SDL_mixer/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX_NATIVE" \
  -DCMAKE_PREFIX_PATH="$PREFIX_NATIVE" \
  -DBUILD_SHARED_LIBS=OFF \
  -DSDL2MIXER_WAV=ON \
  -DSDL2MIXER_MP3=ON \
  -DSDL2MIXER_FLAC=OFF \
  -DSDL2MIXER_VORBIS=OFF \
  -DSDL2MIXER_OPUS=OFF \
  -DSDL2MIXER_MOD=OFF \
  -DSDL2MIXER_MIDI=OFF \
  -DSDL2MIXER_WAVPACK=OFF \
  -DSDL2MIXER_SAMPLES=OFF
cmake --build "$WORK/SDL_mixer/build"
cmake --install "$WORK/SDL_mixer/build"

echo "static SDL2 + SDL2_mixer installed into $PREFIX"
