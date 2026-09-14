#!/bin/bash
# New-install touch layout defaults (preferences.cpp load_preferences).
# Linux / GNU ld (fopen is --wrap'ped to inject an unreadable INI); only
# SDL2 development headers/library are required — the mixer and IDBFS
# seams preferences.cpp reaches are stubbed in the test.
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT=$(mktemp -d /tmp/newtonia-prefs-build.XXXXXX)
trap 'rm -rf "$OUT"' EXIT
${CXX:-g++} -Wall -std=c++11 -I. $(sdl2-config --cflags) \
    test/unit/preferences_test.cpp preferences.cpp atomic_file.cpp $(sdl2-config --libs) \
    -Wl,--wrap=fopen \
    -o "$OUT/preferences_test"
"$OUT/preferences_test"
