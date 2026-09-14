#!/bin/bash
# Linux / GNU ld; only SDL2 development headers/library are required.
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT=$(mktemp -d /tmp/newtonia-save-build.XXXXXX)
trap 'rm -rf "$OUT"' EXIT
${CXX:-g++} -Wall -std=c++11 -I. $(sdl2-config --cflags) \
    test/unit/savegame_test.cpp savegame.cpp atomic_file.cpp $(sdl2-config --libs) \
    -Wl,--wrap=fopen,--wrap=fwrite,--wrap=fclose,--wrap=rename \
    -o "$OUT/savegame_test"
"$OUT/savegame_test"
