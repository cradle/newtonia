# Shared helpers for the headless netplay e2e drivers. See TESTING.md.
#
# Source this from a driver AFTER it has re-exec'd itself under Xvfb
# (see room.sh for the pattern). Provides launch/key/alive/shot/windows
# and a relay reachability check. Everything writes under $OUT.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${NEWTONIA_TEST_OUT:-$(mktemp -d /tmp/newtonia-e2e.XXXXXX)}"
mkdir -p "$OUT"
echo "e2e output: $OUT"

# Isolate saves and preferences: SDL's pref path honours XDG_DATA_HOME,
# so the drivers never touch the developer's real savegame/prefs (and a
# fresh dir means a known menu layout: NEW GAME + ONLINE, no CONTINUE).
export XDG_DATA_HOME="$OUT/xdg"
export SDL_AUDIODRIVER=dummy          # no sound device in CI/containers
export NEWTONIA_NET_DEBUG=1           # the assertions grep NET_LOG output
export NEWTONIA_SIGNAL_URL="${NEWTONIA_SIGNAL_URL:-ws://127.0.0.1:8787/ws}"

[ -x "$ROOT/newtonia" ] || {
  echo "FATAL: $ROOT/newtonia missing - build first: make -j NETPLAY=1"
  exit 1
}

# The room flow needs a signal relay. Default is a local wrangler dev:
#   cd signal && npx wrangler dev --local --port 8787
relay_check() {
  local url
  case "$NEWTONIA_SIGNAL_URL" in
    wss://*) url="https://${NEWTONIA_SIGNAL_URL#wss://}" ;;
    *)       url="http://${NEWTONIA_SIGNAL_URL#ws://}" ;;
  esac
  url="${url%/ws}"
  curl -s --max-time 5 "$url" | grep -q newtonia-signal || {
    echo "FATAL: no signal relay at $NEWTONIA_SIGNAL_URL"
    echo "start one: cd signal && npx wrangler dev --local --port 8787"
    exit 1
  }
}

# launch NAME -> pid; log at $OUT/NAME.log
launch() { "$ROOT/newtonia" > "$OUT/$1.log" 2>&1 & echo $!; }

# alive PID NAME: exit the driver if the process died (139 = SIGSEGV)
alive() { kill -0 "$1" 2>/dev/null || { echo "DEAD: $2"; exit 1; }; }

# key WINDOW KEY: one keypress with settle time
key() { xdotool key --window "$1" "$2"; sleep 0.35; }

# shot WINDOW NAME: screenshot to $OUT/NAME.png (xwd, not import — an
# `import -window root` capture is black under Xvfb once GL is up)
shot() {
  xdotool windowraise "$1"; sleep 0.5
  xwd -id "$1" -out "$OUT/$2.xwd" 2>/dev/null &&
    convert "$OUT/$2.xwd" "$OUT/$2.png" && rm -f "$OUT/$2.xwd"
}

# newtonia_windows: window ids of all game instances, oldest first
newtonia_windows() { xdotool search --name Newtonia | sort -n; }

# host_room_code LOGNAME: poll the host's log for the room code (30 s)
host_room_code() {
  local code="" i
  for i in $(seq 1 30); do
    sleep 1
    code=$(grep -a "\[lobby\] room " "$OUT/$1.log" | tail -1 | awk '{print $3}')
    [ -n "$code" ] && break
  done
  echo "$code"
}

# assert_clean LOG...: fail if any crash/corruption marker appears
assert_clean() {
  local n
  n=$(cat "$@" | grep -aic "decode\|bad_alloc\|segmentation\|assert\|terminate" || true)
  [ "${n:-0}" -eq 0 ] || {
    echo "FAIL: $n error line(s) in logs:"
    grep -ai "decode\|bad_alloc\|segmentation\|assert\|terminate" "$@" | head -5
    exit 1
  }
}
