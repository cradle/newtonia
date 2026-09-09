#!/bin/bash
# One-hand touch gesture layer: the held deflection (touch_controls.h).
# Linux / GNU ld; only SDL2 development headers are required — the clock
# is --wrap'ped and everything else touch_controls.cpp reaches is stubbed
# in the test, so nothing links against the SDL runtime.
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT=$(mktemp -d /tmp/newtonia-onehand-build.XXXXXX)
trap 'rm -rf "$OUT"' EXIT
# SDL_mixer.h is reached through state_manager.h's include chain; linux.yml
# builds SDL2_mixer from source into /usr/local, which sdl2-config's flags
# do not cover, so the same extra include the workflow's own build passes.
SDL_CFLAGS=$(sdl2-config --cflags 2>/dev/null || echo -I/usr/include/SDL2)
${CXX:-g++} -Wall -std=c++11 -I. $SDL_CFLAGS -I/usr/local/include/SDL2 \
    test/unit/touch_one_hand_test.cpp touch_controls.cpp \
    -Wl,--wrap=SDL_GetTicks \
    -o "$OUT/touch_one_hand_test"
"$OUT/touch_one_hand_test"
