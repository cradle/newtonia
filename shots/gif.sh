#!/usr/bin/env bash
# Render clip scenes to looping GIF + MP4 headlessly (Xvfb + the NEWTONIA_SHOT
# harness in clip mode). For the social/Reddit assets — see PROMOTION.md §3.
#
#   shots/gif.sh                                # every shots/clips/*.shot
#   shots/gif.sh shots/clips/invisible.shot     # just one
#   NEWTONIA_GIF_WIDTH=480 shots/gif.sh         # narrower GIF (smaller file)
#   NEWTONIA_GIF_KEEP=1 shots/gif.sh            # keep the PNG frames
#
# Outputs shots/out/clips/<name>.gif and .mp4. Upload the MP4 where the
# target accepts video (Reddit, Bluesky) — it is smaller and cleaner than the
# GIF at the same size; the GIF is the fallback for places that want one.
#
# Wants ./newtonia built (make, or make NETPLAY=0), xvfb-run and ffmpeg.
#
# Playback speed is NOT guessed: the harness quantises frame intervals to its
# fixed 16 ms sim step and logs what it actually used, and this script reads
# that back. So a clip always plays at the speed it was simulated at, even
# when the requested fps did not divide evenly.
set -u
cd "$(dirname "$0")/.."

BIN=./newtonia
[ -x "$BIN" ] || { echo "gif.sh: build ./newtonia first (make NETPLAY=0)"; exit 1; }
command -v xvfb-run >/dev/null || { echo "gif.sh: xvfb-run not found (apt-get install xvfb)"; exit 1; }
command -v ffmpeg   >/dev/null || { echo "gif.sh: ffmpeg not found (apt-get install ffmpeg)"; exit 1; }

OUT=shots/out/clips
mkdir -p "$OUT"

SCENES=("$@")
if [ ${#SCENES[@]} -eq 0 ]; then
  SCENES=(shots/clips/*.shot)
  [ -e "${SCENES[0]}" ] || { echo "gif.sh: no scenes in shots/clips/"; exit 1; }
fi

# Clips are for feeds, not for store pages: 960x540 keeps GIF weight sane
# while staying sharp on a phone. A scene's own `size` line still wins.
DEF_SIZE=960x540
GIF_WIDTH=${NEWTONIA_GIF_WIDTH:-640}

max_w=960 max_h=540
note_size() {
  local w=${1%x*} h=${1#*x}
  [ "$w" -gt "$max_w" ] 2>/dev/null && max_w=$w
  [ "$h" -gt "$max_h" ] 2>/dev/null && max_h=$h
  return 0
}
for s in "${SCENES[@]}"; do
  sz=$(awk '/^size /{gsub("x"," ",$0); print $2 "x" ($3!="" ? $3 : $2)}' "$s" | tail -1)
  [ -n "$sz" ] && note_size "$sz"
done
[ -n "${NEWTONIA_SHOT_SIZE:-}" ] && note_size "$NEWTONIA_SHOT_SIZE"

fail=0
for s in "${SCENES[@]}"; do
  name=$(basename "$s" .shot)
  if [ -n "${NEWTONIA_SHOT_SIZE:-}" ]; then size=$NEWTONIA_SHOT_SIZE
  elif grep -q '^size ' "$s"; then size=""     # the scene decides
  else size=$DEF_SIZE; fi
  grep -q '^frames ' "$s" || { echo "!!! $name: no 'frames' line — not a clip scene"; fail=1; continue; }

  echo "=== $name${size:+ ($size)}"
  frames_dir="$OUT/.$name.frames"
  rm -rf "$frames_dir"; mkdir -p "$frames_dir"

  log=$(env SDL_AUDIODRIVER=dummy \
    NEWTONIA_SHOT="$frames_dir/f.png" \
    NEWTONIA_SHOT_SCENE="$s" \
    ${size:+NEWTONIA_SHOT_SIZE="$size"} \
    timeout 600 xvfb-run -a -s "-screen 0 ${max_w}x${max_h}x24" "$BIN" 2>&1)
  echo "$log" | grep -E "^shot:"

  # Playback rate straight from the harness, not from what we asked for.
  step_ms=$(echo "$log" | sed -n 's/^shot: clip [0-9]* frames @ \([0-9]*\) ms.*/\1/p' | head -1)
  n=$(ls "$frames_dir"/f_*.png 2>/dev/null | wc -l)
  if [ -z "$step_ms" ] || [ "$n" -lt 2 ]; then
    echo "!!! $name: captured $n frames — see the log above"
    fail=1; rm -rf "$frames_dir"; continue
  fi
  fps=$(awk -v s="$step_ms" 'BEGIN{printf "%.4f", 1000/s}')

  # MP4: even dimensions for yuv420p, faststart so it plays while loading.
  ffmpeg -hide_banner -loglevel error -y -framerate "$fps" \
    -i "$frames_dir/f_%04d.png" \
    -vf "scale=trunc(iw/2)*2:trunc(ih/2)*2" \
    -c:v libx264 -pix_fmt yuv420p -crf 18 -movflags +faststart \
    "$OUT/$name.mp4" && echo "shot: wrote $OUT/$name.mp4"

  # GIF: a global palette built from ALL frames (stats_mode=full), else the
  # dark background and thin coloured strokes band badly. sierra2_4a dithers
  # the star field without the crawling that bayer gives on a moving scene.
  ffmpeg -hide_banner -loglevel error -y -framerate "$fps" \
    -i "$frames_dir/f_%04d.png" \
    -lavfi "scale=${GIF_WIDTH}:-1:flags=lanczos,split[a][b];[a]palettegen=stats_mode=full:max_colors=256[p];[b][p]paletteuse=dither=sierra2_4a:diff_mode=rectangle" \
    -loop 0 "$OUT/$name.gif" && echo "shot: wrote $OUT/$name.gif"

  if [ -n "${NEWTONIA_GIF_KEEP:-}" ]; then
    echo "shot: frames kept in $frames_dir ($n)"
  else
    rm -rf "$frames_dir"
  fi
  ls -la "$OUT/$name.gif" "$OUT/$name.mp4" 2>/dev/null | awk '{printf "    %8.2f MB  %s\n", $5/1048576, $9}'
done
exit $fail
