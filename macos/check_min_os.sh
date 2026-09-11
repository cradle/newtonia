#!/bin/bash
# Asserts that every Mach-O given (every architecture slice of it) was built
# for a minimum macOS no NEWER than the floor — the deployment target the
# game ships with (12.0: Makefile OSX_MIN, build_sdl_deps_macos.sh,
# build_netplay_deps.sh, the two macOS workflows).
#
#   macos/check_min_os.sh 12.0 Newtonia.app/Contents/MacOS/newtonia \
#                              Newtonia.app/Contents/Frameworks/*.dylib
#
# A library built WITHOUT a deployment target inherits the building
# machine's OS, and Apple's libc++ headers inline helpers gated on that
# target: a libdatachannel.dylib from a macOS 15 runner referenced
# exception_ptr::__from_native_exception_pointer, which 14 and older have
# no /usr/lib/libc++.1.dylib symbol for, so dyld aborted the Steam build at
# launch on every older Mac (2026-09-11). lipo -verify_arch can't see it —
# both slices were present — so the deploy verify step runs this too.
set -euo pipefail

if [ "$#" -lt 2 ]; then
  echo "usage: $0 <max-minos> <mach-o>..." >&2
  exit 2
fi
FLOOR="$1"; shift

# "12.0" -> 12*10000 + 0*100 for an arithmetic compare (x.y or x.y.z).
key() {
  local IFS=. ; set -- $1
  echo $(( ${1:-0} * 10000 + ${2:-0} * 100 + ${3:-0} ))
}
FLOOR_KEY=$(key "$FLOOR")

fail=0
for f in "$@"; do
  for arch in $(lipo -archs "$f"); do
    # LC_BUILD_VERSION carries "minos"; the older LC_VERSION_MIN_MACOSX
    # (Valve's libsteam_api) carries "version" — take whichever appears.
    minos=$(vtool -arch "$arch" -show-build "$f" \
      | awk '$1 == "minos" { print $2; exit }
             $1 == "cmd" && $2 == "LC_VERSION_MIN_MACOSX" { vm = 1 }
             vm && $1 == "version" { print $2; exit }')
    if [ -z "$minos" ]; then
      echo "FAIL $f ($arch): no minimum-OS load command" >&2
      fail=1
      continue
    fi
    if [ "$(key "$minos")" -gt "$FLOOR_KEY" ]; then
      echo "FAIL $f ($arch): built for macOS $minos, floor is $FLOOR" >&2
      fail=1
    else
      echo "ok   $f ($arch): minos $minos"
    fi
  done
done
exit $fail
