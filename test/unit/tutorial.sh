#!/bin/bash
# Real game/tutorial integration, isolated from player files. Netless build
# in a temporary directory; SDL's offscreen driver also works without Xvfb.
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT=$(mktemp -d /tmp/newtonia-tutorial-test.XXXXXX)
trap 'rm -rf "$OUT"' EXIT
sources=()
for source in *.cpp weapon/*.cpp view/*.cpp xbox/sdl_gdk_stubs.cpp; do
  case "$source" in glut.cpp|android_main.cpp) continue ;; esac
  sources+=("$source")
done
${CXX:-g++} -Wall -std=c++11 -O0 -fno-access-control -I. \
  $(sdl2-config --cflags) -I/usr/local/include/SDL2 \
  test/unit/tutorial_test.cpp "${sources[@]}" \
  -lglut -lGL -lGLU -lX11 -lXi -lSDL2_mixer $(sdl2-config --libs) \
  -o "$OUT/tutorial_test"
export SDL_AUDIODRIVER=dummy
if [ -z "${DISPLAY:-}" ]; then export SDL_VIDEODRIVER=offscreen; fi
for scenario in handoff touch touch-one pause coasting actions persistence controller pad-adopt portrait; do
  if ! XDG_DATA_HOME="$OUT/$scenario" "$OUT/tutorial_test" "$scenario" > "$OUT/$scenario.log" 2>&1; then
    cat "$OUT/$scenario.log"
    exit 1
  fi
  grep 'tutorial_test:' "$OUT/$scenario.log"
done
