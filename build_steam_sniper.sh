#!/bin/sh
# Build newtonia-steam inside Valve's sniper SDK container — the environment
# the shipped Linux depot is built in (deploy-steam.yml, build-linux) — so
# the binary links the Steam runtime's glibc 2.31 instead of the host's.
#
# Why: a host-built newtonia-steam cannot load inside the snap/flatpak Steam
# sandbox, whose base glibc is older than any current distro's ("version
# `GLIBC_2.43' not found (required by newtonia-steam)", field 2026-09-05),
# and glibc is the one library the steam-libs bundle can't carry. Building
# against the runtime is the fix, exactly as the depot does.
#
# Needs docker or podman, and the Steamworks SDK at ./sdk/. Produces the
# same files `make steam` does — newtonia-steam, libsteam_api.so,
# steam_appid.txt, steam-libs/ — so steam-shortcut.sh launches it unchanged.
# Objects are *.sniper.o (STEAM_OBJ_TAG), never mixing with the host build's
# *.steam.o; SDL2 / SDL2_mixer (static, the depot's pins) and libdatachannel
# are built once and cached under ./steam-sniper/ (rm -rf it to rebuild).
# The whole thing mirrors deploy-steam.yml's build-linux job — keep the two
# in step when bumping a pin or a package.
set -eu
cd "$(dirname "$0")"
IMAGE=registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest

if [ "${1:-}" != "--inner" ]; then
  # ---- host side: launch the container ----
  if [ ! -f sdk/public/steam/steam_api.h ]; then
    echo "error: Steamworks SDK not found at ./sdk/ (see the platform-builds skill)" >&2
    exit 1
  fi
  RT=$(command -v docker || command -v podman) || {
    echo "error: needs docker or podman on PATH" >&2; exit 1; }
  mkdir -p steam-sniper
  TTY=""; [ -t 0 ] && TTY="-t"
  # shellcheck disable=SC2086
  exec "$RT" run --rm -i $TTY -v "$PWD:/work" -w /work \
    -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
    -e MAKEFLAGS="${MAKEFLAGS:--j$(nproc 2>/dev/null || echo 4)}" \
    "$IMAGE" sh /work/build_steam_sniper.sh --inner
fi

# ---- container side (root) ----
CACHE=/work/steam-sniper
PREFIX=$CACHE/prefix
NETPREFIX=$CACHE/netplay-libs
export PATH="$PREFIX/bin:$PATH"
touch "$CACHE/.started"
# The version stamp's `git describe` runs on a tree another uid owns.
git config --global --add safe.directory /work

# Network fetches retry with backoff, like the workflow.
try() {
  i=1
  while :; do
    if "$@"; then return 0; fi
    [ "$i" -ge 3 ] && return 1
    echo "attempt $i failed; retrying in $((i*20))s"; sleep $((i*20)); i=$((i+1))
  done
}

echo "== sniper: installing build dependencies"
try apt-get update -qq
try apt-get install -y -qq freeglut3-dev libgl1-mesa-dev libglu1-mesa-dev \
  libssl-dev libxi-dev cmake libudev-dev libasound2-dev libpulse-dev git >/dev/null

if [ ! -f "$PREFIX/lib/libSDL2.a" ]; then
  echo "== sniper: building SDL2 2.32.10 (static)"
  rm -rf "$CACHE/SDL2_source"
  try git clone --depth 1 --branch release-2.32.10 \
    https://github.com/libsdl-org/SDL.git "$CACHE/SDL2_source"
  cmake -S "$CACHE/SDL2_source" -B "$CACHE/SDL2_source/build" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF
  cmake --build "$CACHE/SDL2_source/build" --parallel
  cmake --install "$CACHE/SDL2_source/build"
fi

if [ ! -f "$PREFIX/lib/libSDL2_mixer.a" ]; then
  echo "== sniper: building SDL2_mixer 2.8.2 (static, WAV + MP3)"
  rm -rf "$CACHE/SDL2_mixer"
  try git clone --depth 1 --branch release-2.8.2 \
    https://github.com/libsdl-org/SDL_mixer.git "$CACHE/SDL2_mixer"
  cmake -S "$CACHE/SDL2_mixer" -B "$CACHE/SDL2_mixer/build" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_PREFIX_PATH="$PREFIX" \
    -DSDL2MIXER_WAV=ON -DSDL2MIXER_MP3=ON \
    -DSDL2MIXER_FLAC=OFF -DSDL2MIXER_VORBIS=OFF -DSDL2MIXER_OPUS=OFF \
    -DSDL2MIXER_MOD=OFF -DSDL2MIXER_MIDI=OFF -DSDL2MIXER_WAVPACK=OFF \
    -DBUILD_SHARED_LIBS=OFF -DSDL2MIXER_SAMPLES=OFF
  cmake --build "$CACHE/SDL2_mixer/build" --parallel
  cmake --install "$CACHE/SDL2_mixer/build"
fi

if [ ! -f "$NETPREFIX/include/rtc/rtc.h" ]; then
  echo "== sniper: building libdatachannel (netplay)"
  ./build_netplay_deps.sh "$NETPREFIX"
fi

echo "== sniper: make steam"
# Static SDL2 needs its own dependency list (--static-libs), and X11 after it
# (the Makefile's LIBS names -lX11 -lXi before SDL, which a static libSDL2
# cannot resolve backwards); -lSDL2_mixer goes first for the same reason.
# libstdc++/libgcc_s stay OUT of the bundle here — see STEAM_LIB_SKIP in
# the Makefile. Everything else the container links (freeglut, OpenSSL,
# libdatachannel with its container-only rpath) is bundled into steam-libs/.
make steam STEAM_OBJ_TAG=sniper NETPLAY_PREFIX="$NETPREFIX" \
  SDL2_CFLAGS="$(sdl2-config --cflags)" \
  SDL2_LIBS="-lSDL2_mixer $(sdl2-config --static-libs) -lX11 -lXi" \
  STEAM_LIB_SKIP_EXTRA='libstdc|libgcc_s'

# Hand the outputs back to the host user — ONLY under a real-root runtime
# (docker's daemon), where the files really are root's. Under rootless
# podman the container's uid 0 IS the caller, and a chown to the host uid
# here maps onto a subordinate uid instead, leaving the whole build tree
# owned by a user that doesn't exist on the host (field, 2026-09-05; the
# repair is `podman unshare chown -R 0:0 .`). /proc/self/uid_map tells the
# two apart: "0 0 ..." is real root, "0 <uid> 1" is the rootless mapping.
if [ "$(awk 'NR==1{print $2}' /proc/self/uid_map)" = "0" ]; then
  find /work -newer "$CACHE/.started" -user root \
    -exec chown -h "$HOST_UID:$HOST_GID" {} + 2>/dev/null || true
  chown -R "$HOST_UID:$HOST_GID" "$CACHE" 2>/dev/null || true
fi
echo "== sniper build done: ./newtonia-steam (runtime glibc) + steam-libs/ — launch through ./steam-shortcut.sh"
