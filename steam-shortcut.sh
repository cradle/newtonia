#!/bin/sh
# Launcher for adding the LOCAL Steam build as a non-Steam game WITHOUT a
# Steam runtime compatibility tool (platform-builds skill, "Adding the local
# build as a non-Steam game"). It fixes the working directory (assets are
# CWD-relative) and puts the bundled libraries (`make steam` ->
# ./steam-libs/) first on LD_LIBRARY_PATH: run raw, the shortcut's sandbox
# lacks freeglut, SDL2_mixer and friends. Do NOT use this under the "Steam
# Linux Runtime" compat tool — there the container supplies every library
# and the bundle only gets in the driver's way; point that shortcut at
# newtonia-steam itself. Never shipped — depots come from deploy-steam.yml.
cd "$(dirname "$0")" || exit 1
export LD_LIBRARY_PATH="$PWD/steam-libs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec ./newtonia-steam "$@"
