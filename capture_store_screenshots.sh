#!/bin/bash
# Drive the game headlessly and capture frames for the store pages.
#
#   ./capture_store_screenshots.sh <generation> <tag> [frames-per-weapon] [mode]
#
# mode: "walk" (default) — cycle every primary and secondary, firing at each.
#       "shock"          — fragment the field with the automatic gun, then fire
#                          Shock into the debris (see the note on chaining
#                          below). For the hero/capsule shot.
#
# Starts a new game at <generation> with the full arsenal and screenshots
# mid-burst. Frames land in store_shots/<tag>_NNN.png; a contact sheet goes to
# store_shots/<tag>_sheet.png so a whole run can be reviewed at a glance.
#
# Two things that are easy to get wrong here, both learned the hard way:
#
#   1. A leftover savegame turns NEW GAME into a YES/NO confirm that defaults to
#      NO. A blind Return therefore RESUMES, and a resumed ship never qualifies
#      for the ALL_WEAPONS grant (Ship::give_all_weapons only fires on a bare
#      arsenal) — so you silently capture the old save's level and weapon
#      instead. Every session below deletes savegame.dat first.
#   2. Shock only chains onward from a KILLING hit, so a field of big rocks
#      stops every bolt at the first one and you get a single thin arc. The
#      "shock" mode shreds the field with the automatic base gun first so the
#      bolts have one-shot fragments to chain between.
#
# This is a FRAMING tool, not a capture-of-record tool. It runs under Xvfb with
# software GL, which is fine for finding compositions and checking that a weapon
# reads at all, but final store captures should be taken at 1920x1080 on real
# hardware — llvmpipe's line and shader output is not what a buyer will see.
#
# See STORE.md §5 for the shot list this is meant to serve.
#
# Requires: xvfb-run, xdotool, xwd (x11-apps), convert/montage (imagemagick).
#   sudo apt-get install -y xvfb xdotool x11-apps imagemagick
#
# Because it passes NEWTONIA_BETA / NEWTONIA_START_GENERATION /
# NEWTONIA_ALL_WEAPONS, every run is flagged as cheated — no achievements or
# lifetime stats are banked. Run it against a build made with `make NETPLAY=0`
# unless you specifically need the netplay paths.

set -u

GEN=${1:?usage: capture_store_screenshots.sh <generation> <tag> [frames-per-weapon] [mode]}
TAG=${2:?usage: capture_store_screenshots.sh <generation> <tag> [frames-per-weapon] [mode]}
PER=${3:-3}
MODE=${4:-walk}

HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$HERE/store_shots
GAME=$HERE/newtonia

[ -x "$GAME" ] || { echo "no ./newtonia — run 'make NETPLAY=0' first" >&2; exit 1; }
for t in xvfb-run xdotool xwd convert montage; do
  command -v $t >/dev/null || { echo "missing $t" >&2; exit 1; }
done

mkdir -p "$OUT"

# Re-exec under Xvfb at store resolution if we are not already inside one.
if [ -z "${NEWTONIA_CAPTURE_INSIDE_XVFB:-}" ]; then
  export NEWTONIA_CAPTURE_INSIDE_XVFB=1
  exec xvfb-run -a -s "-screen 0 1920x1080x24" "$0" "$GEN" "$TAG" "$PER" "$MODE"
fi

export SDL_AUDIODRIVER=dummy

# See note 1 in the header: without this the run silently resumes an old save.
rm -f "$HOME/.local/share/cc.gfm/newtonia/savegame.dat"

NEWTONIA_BETA=1 NEWTONIA_START_GENERATION="$GEN" NEWTONIA_ALL_WEAPONS=1 \
  "$GAME" > "$OUT/$TAG.log" 2>&1 &
PID=$!
sleep 4

W=$(xdotool search --name "." 2>/dev/null | tail -1)
[ -n "$W" ] || { echo "game window never appeared — see $OUT/$TAG.log" >&2; kill $PID 2>/dev/null; exit 1; }

K()    { xdotool key --window "$W" "$1" 2>/dev/null; }
shot() { xwd -id "$W" -out "$OUT/.tmp.xwd" 2>/dev/null &&
         convert "$OUT/.tmp.xwd" "$OUT/${TAG}_$(printf '%03d' "$1").png" 2>/dev/null; }
alive() { kill -0 $PID 2>/dev/null; }

K Return; sleep 0.6     # dismiss the attract screen
K Return; sleep 1.5     # NEW GAME  (no save exists on a clean run, so no confirm)
K space;  sleep 1.0     # dismiss the generation's intro screen, if one is due
K space;  sleep 0.8

n=0

if [ "$MODE" = shock ]; then
  # Primary order is 22 gun variants, then Beam, Lance, Shock — 25 in all, with
  # Shock armed at spawn because add_shock splices itself last. So one `q` lands
  # on gun variant 0, and 24 more wrap back round to Shock.
  K q; sleep 0.4
  for i in $(seq 1 70); do                # shred the field with the auto gun
    alive || break
    K space
    [ $((i % 10)) -eq 0 ] && K d
    sleep 0.05
  done
  for _ in $(seq 1 24); do K q; done      # back to Shock
  sleep 0.5

  for f in $(seq 1 $((PER * 10))); do
    alive || break
    K space
    sleep 0.11                            # bolt grows ~126 ms; grab it extended
    n=$((n+1)); shot $n
    [ $((f % 4)) -eq 0 ] && K d
    sleep 0.10
  done

  kill $PID 2>/dev/null; wait $PID 2>/dev/null
  rm -f "$OUT/.tmp.xwd"
  montage "$OUT/${TAG}"_*.png -tile 6x -geometry 300x169+2+2 \
          -background '#222' "$OUT/${TAG}_sheet.png" 2>/dev/null
  echo "captured $n frames -> $OUT/${TAG}_*.png (contact sheet: ${TAG}_sheet.png)"
  echo "Pick by eye from the sheet — no automatic metric reliably finds the good"
  echo "bolts, and the ship's own blue hull outscores a thin arc on every one."
  exit 0
fi

# Primaries: 22 gun variants plus Beam, Lance and Shock (see Ship::give_all_weapons).
for _ in $(seq 1 26); do
  alive || break
  for _ in $(seq 1 "$PER"); do
    K space; K space; K space; K d
    sleep 0.10
    n=$((n+1)); shot $n
  done
  K q; sleep 0.35       # next primary
done

# Secondaries: mine, giga-mine, missile, shield, nova.
for _ in $(seq 1 5); do
  alive || break
  for _ in $(seq 1 "$PER"); do
    K x; K x; K w
    sleep 0.12
    n=$((n+1)); shot $n
  done
  K c; sleep 0.35       # next secondary
done

kill $PID 2>/dev/null; wait $PID 2>/dev/null
rm -f "$OUT/.tmp.xwd"

if [ "$n" -gt 0 ]; then
  montage "$OUT/${TAG}"_*.png -tile 6x -geometry 300x169+2+2 \
          -background '#222' "$OUT/${TAG}_sheet.png" 2>/dev/null
  # Rank by saturation: the frames worth looking at are the ones with a weapon
  # effect on screen. The game is ~92% black, so brightness alone is noise.
  echo "top frames by saturation:"
  for f in "$OUT/${TAG}"_[0-9]*.png; do
    s=$(convert "$f" -colorspace HSL -channel G -separate +channel -format "%[fx:mean]" info: 2>/dev/null)
    echo "$s $(basename "$f")"
  done | sort -rn | head -8
fi

echo "captured $n frames -> $OUT/${TAG}_*.png (contact sheet: ${TAG}_sheet.png)"
