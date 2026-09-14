#!/usr/bin/env bash
# Render the store screenshots at mobile sizes with the touch OSD
# (NEWTONIA_FORCE_TOUCH). Output: shots/out/mobile/<scene>_<device>.png
#
#   shots/mobile.sh                 # all scenes x all devices
#   shots/mobile.sh steam1_level1   # one scene, all devices
#
# The split-screen co-op scene is deliberately absent: local split screen
# does not exist on the touch platforms (mobile co-op is online, one
# full-screen view), so it would misrepresent the product.
#
# Sizes: Apple's required 6.9" iPhone (2868x1320) and 13" iPad
# (2752x2064) landscape shots, plus the optional 6.5" iPhone size in both
# orientations (2688x1242 landscape, 1242x2688 portrait); Play Store
# phone (1920x1080) and tablet (2560x1600).
set -u
cd "$(dirname "$0")/.."

BIN=./newtonia
[ -x "$BIN" ] || { echo "mobile.sh: build ./newtonia first (make NETPLAY=0)"; exit 1; }
command -v xvfb-run >/dev/null || { echo "mobile.sh: xvfb-run not found"; exit 1; }

DEVICES="iphone69:2868x1320 iphone65:2688x1242 iphone65p:1242x2688 ipad13:2752x2064 android:1920x1080 androidtab:2560x1600"
SCENES=("$@")
[ ${#SCENES[@]} -gt 0 ] || SCENES=(menu_mobile steam1_level1 steam3_level5 steam4_level14 steam5_level20)

OUT=shots/out/mobile
mkdir -p "$OUT"

fail=0
for s in "${SCENES[@]}"; do
  scene="shots/$s.shot"
  [ -f "$scene" ] || { echo "mobile.sh: no such scene $scene"; fail=1; continue; }
  for d in $DEVICES; do
    dev=${d%%:*}; size=${d##*:}
    echo "=== $s @ $dev ($size)"
    env SDL_AUDIODRIVER=dummy \
      NEWTONIA_FORCE_TOUCH=1 \
      NEWTONIA_SHOT="$OUT/${s}_${dev}.png" \
      NEWTONIA_SHOT_SCENE="$scene" \
      NEWTONIA_SHOT_SIZE="$size" \
      timeout 180 xvfb-run -a -s "-screen 0 2900x2800x24" "$BIN" \
      2>&1 | grep -E "^shot: (wrote|FAILED|player)" || fail=1
    png="$OUT/${s}_${dev}.png"
    if [ -f "$png" ] && command -v convert >/dev/null; then
      convert "$png" -strip "$png.tmp.png" && mv "$png.tmp.png" "$png"
    fi
  done
done
exit $fail
