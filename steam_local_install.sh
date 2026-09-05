#!/bin/sh
# Run the LOCAL Steam build through the Steam library entry — the fast loop
# for anything Steam Input / overlay / launch-environment related.
#
#   ./build_steam_sniper.sh && ./steam_local_install.sh   # install
#   ./steam_local_install.sh --restore                    # put the depot back
#
# A non-Steam shortcut is not a Steam Input test bed (the shortcut's id and
# steam_appid.txt disagree about who the process is; pads that work from
# the library never reached the game there — platform-builds skill), and a
# beta deploy is a 15-20 minute round trip. This copies the sniper-built
# newtonia-steam over the installed depot's `newtonia` instead: Steam then
# launches it as the real app, under the app's configured runtime, with
# libsteam_api.so, libdatachannel and audio/ already beside it in the depot
# directory (the $ORIGIN rpath finds them). The original is kept as
# newtonia.depot for --restore; Steam's "Verify integrity of game files"
# restores it too. Linux only. Never used by any workflow.
set -eu
cd "$(dirname "$0")"

# Steam library roots, in the order the snap / native / flatpak clients use.
for base in \
  "$HOME/snap/steam/common/.local/share/Steam" \
  "$HOME/.local/share/Steam" \
  "$HOME/.steam/steam" \
  "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam"; do
  [ -f "$base/steamapps/common/Newtonia/newtonia" ] || [ -f "$base/steamapps/common/Newtonia/newtonia.depot" ] && { DEPOT="$base/steamapps/common/Newtonia"; break; }
done
[ -n "${DEPOT:-}" ] || { echo "error: no installed Newtonia depot found under a Steam library" >&2; exit 1; }

if [ "${1:-}" = "--restore" ]; then
  [ -f "$DEPOT/newtonia.depot" ] || { echo "nothing to restore ($DEPOT/newtonia.depot missing)"; exit 0; }
  mv -f "$DEPOT/newtonia.depot" "$DEPOT/newtonia"
  echo "restored the depot binary in $DEPOT"
  exit 0
fi

[ -x newtonia-steam ] || { echo "error: ./newtonia-steam missing — run ./build_steam_sniper.sh first" >&2; exit 1; }
# Refuse a host build: it cannot load inside Steam's runtime container.
need=$(objdump -T newtonia-steam 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)
case "$need" in
  GLIBC_2.3[2-9]*|GLIBC_2.[4-9]*) echo "error: newtonia-steam needs $need — that is a HOST build; use ./build_steam_sniper.sh" >&2; exit 1 ;;
esac
# The depot ships libdatachannel under the soname the binary asks for.
so=$(objdump -p newtonia-steam | awk '/NEEDED.*datachannel/{print $2}')
[ -z "$so" ] || [ -f "$DEPOT/$so" ] || echo "warning: $DEPOT has no $so — the depot was built against another libdatachannel; expect a loader error" >&2

[ -f "$DEPOT/newtonia.depot" ] || cp -p "$DEPOT/newtonia" "$DEPOT/newtonia.depot"
cp -f newtonia-steam "$DEPOT/newtonia"
echo "installed ./newtonia-steam as $DEPOT/newtonia (depot original kept as newtonia.depot)"
echo "launch Newtonia from the Steam library; ./steam_local_install.sh --restore puts the depot back"
