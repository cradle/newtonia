#!/usr/bin/env bash
# Build the store trailer from shots/trailer/*.shot, headless and repeatable.
#
#   shots/trailer.sh                # render every beat, assemble, mux audio
#   NEWTONIA_TRAILER_KEEP=1 ...     # keep each beat's own mp4 and PNG frames
#   NEWTONIA_TRAILER_SIZE=1280x720  # smaller/faster proof cut
#
# Output: shots/out/trailer/newtonia_trailer.mp4 — 1920x1080 H.264 + AAC,
# which is what Steam, YouTube and Reddit all take without transcoding
# complaints.
#
# The beats are numbered scene files and assembled in filename order, so
# re-cutting is renaming, and changing one beat re-renders only that beat's
# frames. Each beat is a normal clip scene (see shots/README.md → Clips);
# nothing here knows anything the clip harness does not.
#
# Audio is the game's own pause-screen theme looped under the whole cut and
# faded at both ends. It is 16 s, so it loops rather than stretches.
#
# Wants ./newtonia built (make, or make NETPLAY=0), xvfb-run and ffmpeg.
set -u
cd "$(dirname "$0")/.."

BIN=./newtonia
[ -x "$BIN" ] || { echo "trailer.sh: build ./newtonia first (make NETPLAY=0)"; exit 1; }
command -v xvfb-run >/dev/null || { echo "trailer.sh: xvfb-run not found"; exit 1; }
command -v ffmpeg   >/dev/null || { echo "trailer.sh: ffmpeg not found"; exit 1; }

OUT=shots/out/trailer
WORK="$OUT/.work"
mkdir -p "$WORK"

SCENES=(shots/trailer/*.shot)
[ -e "${SCENES[0]}" ] || { echo "trailer.sh: no scenes in shots/trailer/"; exit 1; }

SIZE=${NEWTONIA_TRAILER_SIZE:-1920x1080}
SCREEN_W=${SIZE%x*} SCREEN_H=${SIZE#*x}
MUSIC=audio/pause.wav
FINAL="$OUT/newtonia_trailer.mp4"

# ---- 1. render each beat to its own mp4 -------------------------------------
: > "$WORK/concat.txt"
fail=0
for s in "${SCENES[@]}"; do
  name=$(basename "$s" .shot)
  echo "=== $name"
  frames_dir="$WORK/$name.frames"
  rm -rf "$frames_dir"; mkdir -p "$frames_dir"

  log=$(env SDL_AUDIODRIVER=dummy \
    NEWTONIA_SHOT="$frames_dir/f.png" \
    NEWTONIA_SHOT_SCENE="$s" \
    NEWTONIA_SHOT_SIZE="$SIZE" \
    timeout 900 xvfb-run -a -s "-screen 0 ${SCREEN_W}x${SCREEN_H}x24" "$BIN" 2>&1)
  echo "$log" | grep -E "^shot: (clip|player|world|FAILED)"

  step_ms=$(echo "$log" | sed -n 's/^shot: clip [0-9]* frames @ \([0-9]*\) ms.*/\1/p' | head -1)
  n=$(ls "$frames_dir"/f_*.png 2>/dev/null | wc -l)
  if [ -z "$step_ms" ] || [ "$n" -lt 2 ]; then
    echo "!!! $name: captured $n frames — is there a 'frames' line in the scene?"
    fail=1; continue
  fi
  fps=$(awk -v s="$step_ms" 'BEGIN{printf "%.4f", 1000/s}')

  # Constant frame rate and a fixed GOP so the concat demuxer can join the
  # beats without re-encoding artefacts at the seams.
  ffmpeg -hide_banner -loglevel error -y -framerate "$fps" \
    -i "$frames_dir/f_%04d.png" \
    -vf "scale=trunc(iw/2)*2:trunc(ih/2)*2,fps=$fps,format=yuv420p" \
    -c:v libx264 -crf 17 -preset slow -g 30 \
    "$WORK/$name.mp4" || { fail=1; continue; }
  echo "file '$name.mp4'" >> "$WORK/concat.txt"
  printf "    %s frames @ %s ms (%.2f s)\n" "$n" "$step_ms" \
    "$(awk -v n="$n" -v s="$step_ms" 'BEGIN{print n*s/1000}')"
  [ -n "${NEWTONIA_TRAILER_KEEP:-}" ] || rm -rf "$frames_dir"
done
[ "$fail" -eq 0 ] || { echo "trailer.sh: a beat failed — not assembling"; exit 1; }

# ---- 2. join the beats ------------------------------------------------------
ffmpeg -hide_banner -loglevel error -y -f concat -safe 0 \
  -i "$WORK/concat.txt" -c copy "$WORK/silent.mp4" || exit 1
dur=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$WORK/silent.mp4")
echo "=== joined: ${dur}s"

# ---- 3. music bed, looped and faded ----------------------------------------
# -shortest cuts the looped music at the video's end; the fades top and tail
# it so neither the loop seam at the start nor the hard stop at the end is
# audible.
fade_out=$(awk -v d="$dur" 'BEGIN{printf "%.2f", (d>2 ? d-2 : 0)}')
if [ -f "$MUSIC" ]; then
  ffmpeg -hide_banner -loglevel error -y \
    -i "$WORK/silent.mp4" -stream_loop -1 -i "$MUSIC" \
    -filter_complex "[1:a]afade=t=in:st=0:d=1.5,afade=t=out:st=$fade_out:d=2,volume=0.75[a]" \
    -map 0:v -map "[a]" -c:v copy -c:a aac -b:a 192k -shortest \
    -movflags +faststart "$FINAL" || exit 1
else
  echo "trailer.sh: $MUSIC missing — writing silent cut"
  cp "$WORK/silent.mp4" "$FINAL"
fi

[ -n "${NEWTONIA_TRAILER_KEEP:-}" ] || rm -rf "$WORK"
echo
ffprobe -v error -show_entries format=duration,size -of default=nw=1 "$FINAL"
ls -la "$FINAL" | awk '{printf "%.2f MB  %s\n", $5/1048576, $9}'
