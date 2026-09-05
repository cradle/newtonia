#!/bin/sh
# Launcher for adding the LOCAL Steam build as a non-Steam game — add THIS
# script as the game, not newtonia-steam (platform-builds skill, "Adding the
# local build as a non-Steam game"). It fixes the working directory (assets
# are CWD-relative) and puts the bundled host libraries (`make steam` ->
# ./steam-libs/) first on LD_LIBRARY_PATH: the shortcut runs without Steam's
# container, so the sandbox lacks freeglut, SDL2_mixer and friends, and
# Steam's LD_PRELOADed overlay would otherwise drag the sandbox's older
# libstdc++ in ahead of the binary's rpath. Never shipped — depots come from
# deploy-steam.yml.
cd "$(dirname "$0")" || exit 1
export LD_LIBRARY_PATH="$PWD/steam-libs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec ./newtonia-steam "$@"
