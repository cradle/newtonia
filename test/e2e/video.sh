#!/bin/bash
# Video capture harness (shots/README.md "Video capture"), headless:
#   S1  the video pass writes exactly fps x seconds frames, and the stream is
#       exactly frames x W x H x 3 bytes — the encoder is told that geometry
#       and nothing in the stream would say otherwise
#   S2  the audio pass writes one second of s16 per second of run, reports a
#       sync margin of about one mixer buffer, and never warns about lag
#   S3  the same replay renders byte-identically twice (a wall clock anywhere
#       in the frame path shows up here, and twice has)
#   S4  --start/--duration trims: a run started 2 s in produces the requested
#       length and DIFFERENT pixels from the same length at 0
#   S5  shots/video.sh muxes the two passes into a playable mp4 of the right
#       duration with both streams (skipped without ffmpeg)
#   S6  a capture bigger than the virtual screen either refuses or writes
#       whole frames — never a short one the encoder would shear
#   S7  --info reads the header without rendering; asking for both streams at
#       once is refused (they are separate passes)
#   S8  NEWTONIA_VIDEO=- streams frames on stdout with NOTHING else in them —
#       the mechanism Windows needs, where the fifo the driver uses cannot
#       work (MSYS2 emulates fifos for MSYS2 programs; a native .exe cannot
#       open one)
set -u
if [ -z "${DISPLAY:-}" ]; then
  # 640x360 is the capture size below; S6 deliberately asks for more.
  exec xvfb-run -a -s "-screen 0 640x360x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
export NEWTONIA_REPLAY_ENABLE=1

FAIL=0
fail() { echo "FAIL: $*"; FAIL=1; }
ok() { echo "  ok: $*"; }
# Sections report their own verdict: a global FAIL from an earlier section must
# not swallow a later section's ok line.
mark() { SECT=$FAIL; }
ok_if_clean() { [ "$FAIL" = "$SECT" ] && ok "$*"; }
SECT=0

W=320; H=180; FPS=60
RDIR="$XDG_DATA_HOME/cc.gfm/newtonia/replays"

win() { xdotool search --name Newtonia | tail -1; }
P=""
trap 'kill -9 $P 2>/dev/null' EXIT

echo "===== S0: record a run to render ====="
"$ROOT/newtonia" > "$OUT/rec.log" 2>&1 & P=$!
sleep 3
Wd=$(win)
xdotool key --window "$Wd" Return; sleep 0.3      # attract -> menu
xdotool key --window "$Wd" Return; sleep 1.2      # NEW GAME
xdotool key --window "$Wd" space;  sleep 0.5      # spawn
for i in 1 2 3 4 5 6 7 8; do
  xdotool keydown --window "$Wd" w; sleep 0.5; xdotool keyup --window "$Wd" w
  xdotool keydown --window "$Wd" space; sleep 0.9; xdotool keyup --window "$Wd" space
  xdotool keydown --window "$Wd" a; sleep 0.3; xdotool keyup --window "$Wd" a
done
xdotool key --window "$Wd" Escape; sleep 2        # abandon -> header patched
kill -9 $P 2>/dev/null; wait $P 2>/dev/null; P=""
[ -s "$RDIR/current.nrp" ] || { echo "FATAL: no recording to render"; exit 1; }
ok "recorded $(stat -c%s "$RDIR/current.nrp") bytes"

# render PASS OUTPUT [extra env...]
render() {
  local kind=$1 out=$2; shift 2
  env NEWTONIA_VIDEO_REPLAY=current NEWTONIA_VIDEO_SIZE="${W}x${H}" \
      NEWTONIA_VIDEO_FPS=$FPS "$@" \
      ${kind:+NEWTONIA_VIDEO$kind="$out"} \
      "$ROOT/newtonia" 2>&1
}

mark
echo "===== S1: video pass geometry ====="
render "" "$OUT/a.raw" NEWTONIA_VIDEO="$OUT/a.raw" NEWTONIA_VIDEO_MS=3000 \
  > "$OUT/v1.log" 2>&1
frames=$(sed -n 's/.*video: wrote \([0-9]*\) frames.*/\1/p' "$OUT/v1.log")
want_frames=$((FPS * 3))
bytes=$(stat -c%s "$OUT/a.raw" 2>/dev/null || echo 0)
[ "$frames" = "$want_frames" ] || fail "S1 frames=$frames want $want_frames"
[ "$bytes" = "$((frames * W * H * 3))" ] || fail "S1 stream is $bytes bytes, not $((frames * W * H * 3))"
ok_if_clean "$frames frames, $bytes bytes, exactly ${W}x${H}x3 each"

mark
echo "===== S2: audio pass rate and sync margin ====="
render "_AUDIO" "$OUT/a.pcm" NEWTONIA_VIDEO_MS=3000 > "$OUT/v2.log" 2>&1
abytes=$(stat -c%s "$OUT/a.pcm" 2>/dev/null || echo 0)
# 44100 Hz stereo s16 = 176400 B/s; allow one mixer buffer either way.
lo=$((176400 * 3 - 8192)); hi=$((176400 * 3 + 8192))
[ "$abytes" -ge "$lo" ] && [ "$abytes" -le "$hi" ] \
  || fail "S2 audio is $abytes bytes, want ${lo}..${hi} (3 s)"
margin=$(sed -n 's/.*sync margin \([0-9]*\) ms.*/\1/p' "$OUT/v2.log")
[ -n "$margin" ] && [ "$margin" -le 60 ] \
  || fail "S2 sync margin '$margin' ms is not within one mixer buffer"
grep -q "WARNING" "$OUT/v2.log" && fail "S2 the audio pass warned: $(grep WARNING "$OUT/v2.log")"
ok "$abytes bytes of audio, sync margin ${margin} ms"

mark
echo "===== S3: two renders are byte-identical ====="
render "" "$OUT/b.raw" NEWTONIA_VIDEO="$OUT/b.raw" NEWTONIA_VIDEO_MS=3000 \
  > "$OUT/v3.log" 2>&1
cmp -s "$OUT/a.raw" "$OUT/b.raw" \
  && ok "identical frame streams" \
  || fail "S3 the same replay rendered differently twice (a wall clock in the frame path?)"

mark
echo "===== S4: --start / --duration ====="
render "" "$OUT/c.raw" NEWTONIA_VIDEO="$OUT/c.raw" NEWTONIA_VIDEO_START_MS=2000 \
  NEWTONIA_VIDEO_MS=1000 > "$OUT/v4.log" 2>&1
cframes=$(sed -n 's/.*video: wrote \([0-9]*\) frames.*/\1/p' "$OUT/v4.log")
[ "$cframes" = "$FPS" ] || fail "S4 trimmed render has $cframes frames, want $FPS"
grep -q "capture starts at 2" "$OUT/v4.log" || fail "S4 no skip-ahead line"
# The first frame of the trimmed render must not be the first frame of the
# whole one, or the skip did nothing.
head -c $((W * H * 3)) "$OUT/a.raw" > "$OUT/f0.bin"
head -c $((W * H * 3)) "$OUT/c.raw" > "$OUT/f2.bin"
cmp -s "$OUT/f0.bin" "$OUT/f2.bin" && fail "S4 the skip-ahead rendered the same first frame"
ok_if_clean "$cframes frames starting 2 s in"

mark
echo "===== S5: shots/video.sh end to end ====="
if command -v ffmpeg >/dev/null && command -v ffprobe >/dev/null; then
  "$ROOT/shots/video.sh" --replay current --size "${W}x${H}" --duration 3 \
    --out "$OUT/out.mp4" > "$OUT/v5.log" 2>&1 \
    || fail "S5 video.sh failed: $(tail -3 "$OUT/v5.log")"
  if [ -s "$OUT/out.mp4" ]; then
    dur=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$OUT/out.mp4")
    streams=$(ffprobe -v error -show_entries stream=codec_type -of csv=p=0 "$OUT/out.mp4" | sort | tr '\n' ' ')
    case "$dur" in 3.*|2.9*) ok "mp4 is ${dur}s, streams: $streams" ;;
      *) fail "S5 mp4 duration $dur, want ~3" ;;
    esac
    echo "$streams" | grep -q audio || fail "S5 mp4 has no audio stream"
    echo "$streams" | grep -q video || fail "S5 mp4 has no video stream"
  else
    fail "S5 no mp4 written"
  fi
else
  echo "  SKIP: ffmpeg/ffprobe not installed"
fi

mark
echo "===== S6: a capture bigger than the screen ====="
# The Xvfb screen is 640x360 (top of this file). Bare Xvfb has no window
# manager, so the window is usually created oversized anyway and the render
# just works; a real desktop clamps it, and then the harness must REFUSE —
# a raw stream carries no geometry, so short frames would shear the video.
# Either outcome is fine. What must never happen is a stream whose length
# isn't a whole number of full frames.
render "" "$OUT/big.raw" NEWTONIA_VIDEO="$OUT/big.raw" NEWTONIA_VIDEO_SIZE=1920x1080 \
  NEWTONIA_VIDEO_MS=200 > "$OUT/v6.log" 2>&1
if grep -q "was requested" "$OUT/v6.log"; then
  ok "refused a clamped window"
else
  bf=$(sed -n 's/.*video: wrote \([0-9]*\) frames.*/\1/p' "$OUT/v6.log")
  bb=$(stat -c%s "$OUT/big.raw" 2>/dev/null || echo 0)
  [ -n "$bf" ] && [ "$bb" = "$((bf * 1920 * 1080 * 3))" ] \
    && ok "rendered ${bf} whole 1920x1080 frames (no window manager to clamp)" \
    || fail "S6 wrote $bb bytes for $bf frames of 1920x1080 - short frames"
fi

mark
echo "===== S7: --info, and one stream at a time ====="
env NEWTONIA_VIDEO=/dev/null NEWTONIA_VIDEO_REPLAY=current NEWTONIA_VIDEO_INFO=1 \
  "$ROOT/newtonia" > "$OUT/v7.log" 2>&1
grep -q "score .*level .*player" "$OUT/v7.log" || fail "S7 --info printed no header"
grep -q "video: wrote" "$OUT/v7.log" && fail "S7 --info rendered something"
env NEWTONIA_VIDEO="$OUT/x.raw" NEWTONIA_VIDEO_AUDIO="$OUT/x.pcm" \
  NEWTONIA_VIDEO_REPLAY=current "$ROOT/newtonia" > "$OUT/v8.log" 2>&1
grep -q "separate passes" "$OUT/v8.log" \
  || fail "S7 asking for both streams at once was not refused"
ok_if_clean "header-only run, and both-streams refused"

mark
echo "===== S8: frames on stdout ====="
# stdout carries the frames, stderr carries every log line. The game writes
# through a DUPLICATE of stdout and points stdout itself at stderr, so a
# logger nobody remembered cannot land in the middle of a frame — which is
# exactly what happened while building this: SDL_Log and several std::cout
# lines went to stdout and put 487 bytes of text in the stream.
#
# Note the two streams must be separated HERE, per invocation. Wrapping the
# game in xvfb-run merges them, which silently defeats the whole scheme (and
# cost an hour of chasing a phantom bug).
env NEWTONIA_VIDEO=- NEWTONIA_VIDEO_REPLAY=current NEWTONIA_VIDEO_SIZE="${W}x${H}" \
    NEWTONIA_VIDEO_FPS=$FPS NEWTONIA_VIDEO_MS=1000 \
    "$ROOT/newtonia" 2> "$OUT/v8.log" > "$OUT/pipe.raw"
praw=$(stat -c%s "$OUT/pipe.raw" 2>/dev/null || echo 0)
pframe=$((W * H * 3))
[ "$((praw % pframe))" = "0" ] && [ "$((praw / pframe))" = "$FPS" ] \
  || fail "S8 stdout stream is $praw bytes: $((praw / pframe)) frames + $((praw % pframe)) bytes of something else"
grep -q "video: wrote" "$OUT/v8.log" || fail "S8 the summary did not go to stderr"
# Byte-identical to the same render written to a file: same frames, no framing
# differences from the pipe.
render "" "$OUT/d.raw" NEWTONIA_VIDEO="$OUT/d.raw" NEWTONIA_VIDEO_MS=1000 > /dev/null 2>&1
cmp -s "$OUT/pipe.raw" "$OUT/d.raw" || fail "S8 stdout frames differ from file frames"
ok_if_clean "$((praw / pframe)) clean frames on stdout, logs on stderr"

echo
[ "$FAIL" = "0" ] && echo "VIDEO E2E PASS" || echo "VIDEO E2E FAIL"
exit $FAIL
