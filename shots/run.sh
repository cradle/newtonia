#!/usr/bin/env bash
# Render scene scripts to PNGs headlessly (Xvfb + the NEWTONIA_SHOT harness).
#
#   shots/run.sh                          # every shots/*.shot -> shots/out/
#   shots/run.sh shots/hero.shot          # just one
#   NEWTONIA_SHOT_SIZE=2560x1440 shots/run.sh shots/hero.shot   # resize
#
# Wants ./newtonia built (make, or make NETPLAY=0) and xvfb-run on PATH.
# The Xvfb screen is sized to fit the largest requested shot automatically.
set -u
cd "$(dirname "$0")/.."

BIN=./newtonia
[ -x "$BIN" ] || { echo "run.sh: build ./newtonia first (make NETPLAY=0)"; exit 1; }
command -v xvfb-run >/dev/null || { echo "run.sh: xvfb-run not found (apt-get install xvfb)"; exit 1; }

OUT=shots/out
mkdir -p "$OUT"

SCENES=("$@")
[ ${#SCENES[@]} -gt 0 ] || SCENES=(shots/*.shot)

# A scene without its own `size` line renders at this default (instead of
# whatever window size the local preferences file happens to hold).
DEF_SIZE=1280x800

# Size the virtual screen to the largest shot being rendered.
max_w=1280 max_h=800
note_size() {
  local w=${1%x*} h=${1#*x}
  [ "$w" -gt "$max_w" ] 2>/dev/null && max_w=$w
  [ "$h" -gt "$max_h" ] 2>/dev/null && max_h=$h
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
  echo "=== $name${size:+ ($size)}"
  env SDL_AUDIODRIVER=dummy \
    NEWTONIA_SHOT="$OUT/$name.png" \
    NEWTONIA_SHOT_SCENE="$s" \
    ${size:+NEWTONIA_SHOT_SIZE="$size"} \
    timeout 120 xvfb-run -a -s "-screen 0 ${max_w}x${max_h}x24" "$BIN" \
    2>&1 | grep -E "^shot:" || fail=1
done
exit $fail
