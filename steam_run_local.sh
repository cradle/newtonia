#!/bin/bash
# Run the LOCAL Steam build through the Steam LIBRARY entry without touching
# the installed depot. Set the library entry's Launch Options to:
#
#   /home/<you>/newtonia/steam_run_local.sh %command%
#
# (env vars in front work as usual: NEWTONIA_TRACE=1 /path/steam_run_local.sh %command%)
#
# Steam expands %command% to its whole launch line — the launch wrapper,
# the reaper, the runtime container's entry point, and LAST the depot
# binary. This swaps that last argument for ./newtonia-steam and execs the
# rest unchanged, so Steam still launches the local build as the REAL app,
# under the app's configured runtime, with the depot as the working
# directory (audio/ is CWD-relative and the depot's is the same tree).
# libsteam_api.so resolves through the binary's $ORIGIN rpath to this
# checkout, libdatachannel through its absolute steam-sniper rpath (the
# home directory is shared into the runtime container). Needs a
# runtime-built binary (./build_steam_sniper.sh) — a host build cannot load
# inside the container. stdout/stderr go to ./newtonia-steam.log because
# Steam swallows both. Linux only; never used by any workflow.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
if [ $# -lt 1 ]; then
  echo "usage: set the library entry's Launch Options to: $0 %command%" >&2
  exit 1
fi
if [ ! -x "$HERE/newtonia-steam" ]; then
  echo "error: $HERE/newtonia-steam missing — run ./build_steam_sniper.sh first" >&2
  exit 1
fi
args=("$@")
args[$((${#args[@]}-1))]="$HERE/newtonia-steam"
{
  echo "== $(date) launching: ${args[*]}"
  exec "${args[@]}"
} >"$HERE/newtonia-steam.log" 2>&1
