#!/usr/bin/env bash
# Render a recorded replay to an MP4 — gameplay video for a store page,
# reproducible from the command line (see shots/README.md "Video capture").
#
#   shots/video.sh                                   # best.nrp -> shots/out/gameplay.mp4
#   shots/video.sh --replay recent --out promo.mp4
#   shots/video.sh --start 0:30 --duration 1:00      # trim to a highlight
#   shots/video.sh --info                            # what's in the file?
#
# The game renders every frame on its own fixed clock and streams raw rgb24
# through a fifo to ffmpeg, so the output is a clean 60 fps regardless of what
# the machine managed — this is a render, not a screen recording.
#
# Picture and sound are two passes over the same replay, run in parallel (see
# video_capture.h for why they cannot be one): the video pass renders as fast
# as the machine allows, the audio pass replays the same file without drawing
# and captures the mix at the audio device's own rate, so it takes the run's
# real duration. They line up because both walk the same records on the same
# fixed timestep; this script muxes them at the end.
#
# Wants ./newtonia built (make, or make NETPLAY=0), ffmpeg, and — headless —
# xvfb-run. Under Xvfb the GL is software, so expect the video pass to run well
# under real time on a long capture; it is still every frame.
set -u
cd "$(dirname "$0")/.."

BIN=./newtonia
REPLAY=best
OUT=shots/out/gameplay.mp4
SIZE=1920x1080
FPS=60
START=0
DURATION=0
AUDIO=1
HUD=1
CHROME=0
CRF=16
INFO=0
KEEP=0

usage() {
  sed -n '2,23p' "$0" | sed 's/^# \{0,1\}//'
  cat <<'EOF'

Options:
  --replay NAME   best (default) | recent | current | bestcoop | online | last
                  | a path to a .nrp file
  --out FILE      output .mp4 (default shots/out/gameplay.mp4)
  --size WxH      frame size (default 1920x1080)
  --fps N         frame rate (default 60)
  --start T       skip T into the run before capturing (S, M:SS, or ms with
                  a trailing "ms")
  --duration T    capture T of the run (default: to the end of the recording)
  --crf N         x264 quality, lower is better (default 16, visually lossless)
  --no-audio      render silent
  --no-hud        drop the in-game HUD and minimap (pure world)
  --chrome        keep the REPLAY watermark, timeline and control hints
  --keep          keep the intermediate frames/audio next to the output
  --info          print the replay's header (score, level, length) and exit
EOF
}

# T -> milliseconds. Accepts 90, 1:30, 500ms.
to_ms() {
  case "$1" in
    *ms)  echo "${1%ms}" ;;
    *:*)  echo $(( ${1%%:*} * 60000 + ${1##*:} * 1000 )) ;;
    *)    echo $(( $1 * 1000 )) ;;
  esac
}

while [ $# -gt 0 ]; do
  case "$1" in
    --replay)   REPLAY=$2; shift 2 ;;
    --out)      OUT=$2; shift 2 ;;
    --size)     SIZE=$2; shift 2 ;;
    --fps)      FPS=$2; shift 2 ;;
    --start)    START=$(to_ms "$2"); shift 2 ;;
    --duration) DURATION=$(to_ms "$2"); shift 2 ;;
    --crf)      CRF=$2; shift 2 ;;
    --no-audio) AUDIO=0; shift ;;
    --no-hud)   HUD=0; shift ;;
    --chrome)   CHROME=1; shift ;;
    --keep)     KEEP=1; shift ;;
    --info)     INFO=1; shift ;;
    -h|--help)  usage; exit 0 ;;
    *) echo "video.sh: unknown option $1"; usage; exit 2 ;;
  esac
done

[ -x "$BIN" ] || { echo "video.sh: build ./newtonia first (make NETPLAY=0)"; exit 1; }

W=${SIZE%x*}; H=${SIZE#*x}
mkdir -p "$(dirname "$OUT")"

# Headless needs a virtual screen at least as big as the frame: a clamped
# window would change the frame geometry, which a raw stream cannot express
# (the game refuses rather than shearing the video — see VideoCapture::capture).
# NICE_RUN (set per call) deprioritises a pass against the real-time one.
run_game() {
  if [ -n "${DISPLAY:-}" ]; then
    ${NICE_RUN:-} env "$@" "$BIN"
  else
    command -v xvfb-run >/dev/null || {
      echo "video.sh: no DISPLAY and no xvfb-run (apt-get install xvfb)"; exit 1; }
    ${NICE_RUN:-} env "$@" xvfb-run -a -s "-screen 0 ${W}x${H}x24" "$BIN"
  fi
}

if [ "$INFO" = "1" ]; then
  run_game NEWTONIA_VIDEO=/dev/null NEWTONIA_VIDEO_REPLAY="$REPLAY" \
           NEWTONIA_VIDEO_INFO=1 2>&1 | grep -E "^(INFO: )?video:"
  exit ${PIPESTATUS[0]}
fi

command -v ffmpeg >/dev/null || { echo "video.sh: ffmpeg not found"; exit 1; }

WORK=$(dirname "$OUT")/.$(basename "$OUT" .mp4).work
rm -rf "$WORK"; mkdir -p "$WORK"
FIFO=$WORK/frames.raw
RAW=$WORK/audio.raw
SILENT=$WORK/silent.mp4
mkfifo "$FIFO"
cleanup() { [ "$KEEP" = "1" ] || rm -rf "$WORK"; }
trap cleanup EXIT

COMMON=(
  NEWTONIA_VIDEO_REPLAY="$REPLAY"
  NEWTONIA_VIDEO_SIZE="${W}x${H}"
  NEWTONIA_VIDEO_FPS="$FPS"
  NEWTONIA_VIDEO_START_MS="$START"
  NEWTONIA_VIDEO_MS="$DURATION"
  NEWTONIA_VIDEO_HUD="$HUD"
  NEWTONIA_VIDEO_CHROME="$CHROME"
)

echo "=== rendering $REPLAY -> $OUT (${W}x${H} @ ${FPS}fps)"

# The audio pass first, in the background: it runs at the replay's own speed,
# so it is usually the shorter of the two and costs nothing to overlap.
#
# It is also the only REAL-TIME half, and that makes it the one to protect.
# Overlapped without this, the render and x264 saturate every core and stall
# the paced sim: measured on a 4-core box, a late-game 1080p clip reported a
# 135 ms sync margin overlapped and 23 ms — one mixer buffer, the floor —
# running alone. So the video side is niced below it; the audio pass needs
# barely a tenth of a core (12 s of CPU for a 90 s clip) and simply has to
# get it on time.
NICE=""
command -v nice >/dev/null && NICE="nice -n 19"
NICE_RUN=""   # set only for the video pass, below
AP=""
if [ "$AUDIO" = "1" ]; then
  ( run_game "${COMMON[@]}" NEWTONIA_VIDEO_AUDIO="$RAW" \
      > "$WORK/audio.log" 2>&1 ) &
  AP=$!
fi

# The encoder must be reading before the game opens the fifo — opening a fifo
# for writing blocks until there is a reader.
$NICE ffmpeg -hide_banner -loglevel warning -y \
  -f rawvideo -pix_fmt rgb24 -s "${W}x${H}" -r "$FPS" -i "$FIFO" \
  -an -c:v libx264 -preset slow -crf "$CRF" -pix_fmt yuv420p \
  -movflags +faststart "$SILENT" &
FF=$!

set +e
NICE_RUN=$NICE
run_game "${COMMON[@]}" NEWTONIA_VIDEO="$FIFO" 2>&1 \
  | grep -E "^(INFO: )?(video|replay):"
GAME=${PIPESTATUS[0]}
# A video pass that died before opening the fifo (an unreadable replay, a bad
# size) leaves ffmpeg blocked forever on a reader-less fifo — it must not
# outlive the pass it was encoding.
[ "$GAME" = "0" ] || kill $FF 2>/dev/null
wait $FF; ENC=$?
APX=0
if [ -n "$AP" ]; then
  wait $AP; APX=$?
  grep -E "^(INFO: )?video:" "$WORK/audio.log" | sed 's/^/audio pass: /'
fi
set -e
if [ "$GAME" != "0" ] || [ "$ENC" != "0" ]; then
  echo "video.sh: FAILED (video pass exit $GAME, ffmpeg exit $ENC)"
  exit 1
fi

# Mux the captured audio under the finished video. The video is copied, not
# re-encoded — this pass costs seconds, not another render.
if [ "$AUDIO" = "1" ] && [ "$APX" = "0" ] && [ -s "$RAW" ]; then
  # Rate and channel count from what the pass actually opened, not from a
  # guess: the raw stream carries no format, so a mixer opened differently
  # would otherwise be muxed at the wrong speed.
  SPEC=$(sed -n 's/.*audio \([0-9]*\) Hz \([0-9]*\) ch.*/\1 \2/p' "$WORK/audio.log")
  ARATE=${SPEC% *}; ACH=${SPEC#* }
  ffmpeg -hide_banner -loglevel error -y -i "$SILENT" \
    -f s16le -ar "${ARATE:-44100}" -ac "${ACH:-2}" -i "$RAW" \
    -c:v copy -c:a aac -b:a 320k -shortest -movflags +faststart "$OUT"
else
  [ "$AUDIO" = "1" ] && echo "video.sh: the audio pass produced nothing - writing silent"
  mv "$SILENT" "$OUT"
fi

echo "video.sh: wrote $OUT"
ffprobe -hide_banner -loglevel error -show_entries \
  format=duration,size:stream=codec_name,width,height,r_frame_rate \
  -of default=noprint_wrappers=1 "$OUT" 2>/dev/null || true
