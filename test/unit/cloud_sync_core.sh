#!/bin/bash
# The shared cloud sync driver against a fake store (cloud_sync_core.h).
# Linux; needs only the SDL2 development package.
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT=$(mktemp -d /tmp/newtonia-cloud-core.XXXXXX)
trap 'rm -rf "$OUT"' EXIT
${CXX:-g++} -Wall -std=c++11 -I. $(sdl2-config --cflags) \
    test/unit/cloud_sync_core_test.cpp cloud_sync_core.cpp atomic_file.cpp \
    $(sdl2-config --libs) -o "$OUT/cloud_sync_core_test"
"$OUT/cloud_sync_core_test"
