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
# Since the B7 cap flip the DEFAULT seat cap is 4, which routes every host
# through the B4b waiting room (a pair no longer auto-starts — the host
# presses START). The classic 2P drivers predate that flow and assemble
# bespoke pairs, so pin them to the pairwise flow unless a driver already
# chose its own seat count (the N-seat drivers export 3/4 before launch;
# pairstart.sh unsets this to cover the shipping default-cap pair flow).
export NEWTONIA_NET_TEST_SEATS="${NEWTONIA_NET_TEST_SEATS:-2}"

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

# launch NAME [VAR=VAL ...] -> pid; log at $OUT/NAME.log. Extra args are
# per-instance env assignments (room_setup uses this to give ONLY the host
# a test hook — exporting the hook before launch would arm every joiner,
# and a client-side kill hook fights the snapshot restore).
launch() {
  local name=$1; shift
  env "$@" "$ROOT/newtonia" > "$OUT/$name.log" 2>&1 & echo $!
}

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

# wait_no_windows: poll until every game window is gone (15 s cap). Use
# between sequential pairings in one Xvfb session — a lingering window from
# the previous pairing otherwise gets scooped up by newtonia_windows and
# eats the next pairing's keystrokes.
wait_no_windows() {
  local i
  for i in $(seq 1 15); do
    [ -z "$(newtonia_windows)" ] && return 0
    sleep 1
  done
  echo "stale game windows never closed"; exit 1
}

# kill_pair PID PID: SIGTERM (clean quit through the SDL_QUIT path — saves
# run, an online host says BYE) with a SIGKILL fallback so a wedged
# instance can never hang the driver's wait/teardown. Logs are already
# written by teardown time; the hard kill loses nothing.
kill_pair() {
  kill "$@" 2>/dev/null; sleep 2
  kill -9 "$@" 2>/dev/null; wait "$@" 2>/dev/null
}

# nav_host WINDOW: attract -> menu -> ONLINE -> HOST (fresh-prefs menu
# layout: NEW GAME, ONLINE, OPTIONS — no CONTINUE).
nav_host() {
  key "$1" Return; sleep 1; key "$1" s; key "$1" Return; sleep 1; key "$1" Return
}

# nav_join WINDOW CODE: attract -> menu -> ONLINE -> JOIN -> type the code
# (the join fires when the fifth character lands).
nav_join() {
  local c
  key "$1" Return; sleep 1; key "$1" s; key "$1" Return; sleep 1
  key "$1" s; key "$1" Return; sleep 1
  for c in $(echo "$2" | grep -o .); do key "$1" "$c"; done
}

# host_room_code LOGNAME: poll the host's log for the room code (30 s)
host_room_code() {
  local code="" i
  for i in $(seq 1 30); do
    sleep 1
    # sed, not a fixed awk field: NET_LOG lines carry a "host: " role
    # prefix now, so positional extraction would grab the wrong word.
    code=$(grep -a "\[lobby\] room " "$OUT/$1.log" | tail -1 |
           sed 's/.*\[lobby\] room \([^ ]*\).*/\1/')
    [ -n "$code" ] && break
  done
  echo "$code"
}

# skip_to_generation WINDOW LOGNAME TARGET: drive the host to generation
# TARGET with the skip-level key, and CONFIRM it got there.
#
# The obvious loop — press n TARGET times — lands short whenever Xvfb drops a
# keystroke under load, and the failure surfaces nowhere near its cause: a
# press lost on the way to gen 9 reads as "joiner never replicated the
# pulsar", i.e. exactly like a netplay bug. Two of three retried drivers in
# the first CI shard run failed this way (2026-08-12).
#
# The generation is only observable through the host's 10 s snapshot
# telemetry (`net: slot #N gen=G`), so the correction waits for a FRESH slot
# line before topping up — reading a stale one would press n again and
# overshoot the level the driver wants to sit on.
skip_to_generation() {
  local w=$1 log=$2 target=$3 i gen before now tries
  for i in $(seq 1 "$target"); do key "$w" n; sleep 3; done
  for tries in 1 2 3; do
    before=$(grep -ac "slot #" "$OUT/$log.log")
    for i in $(seq 1 15); do
      now=$(grep -ac "slot #" "$OUT/$log.log")
      [ "$now" -gt "$before" ] && break
      sleep 1
    done
    gen=$(grep -a "slot #" "$OUT/$log.log" | tail -1 |
          sed 's/.*gen=\([0-9]*\).*/\1/')
    [ -n "$gen" ] || return 1
    [ "$gen" -ge "$target" ] && return 0
    echo "== skip correction: at generation $gen, want $target"
    for i in $(seq 1 $((target - gen))); do key "$w" n; sleep 3; done
  done
  return 1
}

# ---- N-seat room helpers (FOURPLAYER.md B6) ----
# A "room" is one host + (N-1) relay joiners assembled through the B4b
# waiting room. Callers export NEWTONIA_NET_TEST_SEATS=N before the first
# launch. State lives in three globals: ROOM_CODE, and ROOM_PIDS/ROOM_WINS
# indexed 0..N-1 — index 0 is the host (log host.log), index i>=1 is
# joiner$i (log joiner$i.log), seated at i+1 in join order. A driver that
# kills an instance clears its slot so room_alive/room_kill_all skip it.

# room_name I: display/log name for roster index I
room_name() { [ "$1" = 0 ] && echo host || echo "joiner$1"; }

# new_window_since "BEFORE": the window id not in the BEFORE list. The
# BEFORE/AFTER diff, not an exclusion list — a killed window can linger,
# or X can recycle its id for the new client (see threeseat_rejoin, B5).
new_window_since() {
  local w new=
  for w in $(newtonia_windows); do
    echo "$1" | grep -q "^$w$" || new=$w
  done
  echo "$new"
}

# room_fail MSG [LOG]: dump context, tear the room down, exit
room_fail() {
  echo "$1"
  [ -n "${2:-}" ] && tail -20 "$OUT/$2.log"
  room_kill_all
  exit 1
}

# room_joiner_env I: per-joiner env assignments for room_setup's launches
# (space-free VAR=VAL words on stdout). Drivers override it to give each
# joiner instance its own env — e.g. a distinct NEWTONIA_NET_NAME so the
# rejoin-by-identity door can tell the pilots apart (nseat_swap.sh).
# Default: nothing.
room_joiner_env() { :; }

# room_setup N [HOSTVAR=VAL ...]: assemble the room and wait until it is
# PLAYING — every seat filled, auto-started on full, every joiner
# bootstrapped. Extra args become host-only env (test hooks).
room_setup() {
  local n=$1 i seat before ok
  shift
  ROOM_PIDS=(); ROOM_WINS=(); ROOM_CODE=
  ROOM_PIDS[0]=$(launch host "$@")
  sleep 2
  ROOM_WINS[0]=$(newtonia_windows | head -1)
  [ -n "${ROOM_WINS[0]}" ] || room_fail "NO HOST WINDOW" host
  nav_host "${ROOM_WINS[0]}"
  ROOM_CODE=$(host_room_code host)
  [ -n "$ROOM_CODE" ] || room_fail "NO ROOM CODE" host
  echo "room code: $ROOM_CODE"
  for i in $(seq 1 $((n - 1))); do
    before=$(newtonia_windows)
    ROOM_PIDS[$i]=$(launch "joiner$i" $(room_joiner_env "$i"))
    sleep 4
    ROOM_WINS[$i]=$(new_window_since "$before")
    [ -n "${ROOM_WINS[$i]}" ] || room_fail "NO WINDOW FOR joiner$i" "joiner$i"
    nav_join "${ROOM_WINS[$i]}" "$ROOM_CODE"
    seat=$((i + 1)); ok=
    for _ in $(seq 1 40); do
      grep -aq "seat $seat filled" "$OUT/host.log" && { ok=1; break; }
      sleep 1
    done
    [ -n "$ok" ] || room_fail "SEAT $seat NEVER FILLED" host
    echo "seat $seat filled"
  done
  ok=
  for _ in $(seq 1 40); do
    grep -aq "starting with $((n - 1)) peer" "$OUT/host.log" && { ok=1; break; }
    sleep 1
  done
  [ -n "$ok" ] || room_fail "NO AUTO-START" host
  for i in $(seq 1 $((n - 1))); do
    ok=
    for _ in $(seq 1 30); do
      grep -aq "bootstrap adopted" "$OUT/joiner$i.log" && { ok=1; break; }
      sleep 1
    done
    [ -n "$ok" ] || room_fail "JOINER$i NO BOOTSTRAP" "joiner$i"
  done
  echo "$n-seat room up (host + $((n - 1)) peers)"
}

# room_alive: every still-tracked instance must be running. Unlike bare
# alive(), a death tears the whole room down (room_fail) instead of
# leaving N-1 instances to die of X-server loss with the driver.
room_alive() {
  local i
  for i in "${!ROOM_PIDS[@]}"; do
    [ -n "${ROOM_PIDS[$i]}" ] || continue
    kill -0 "${ROOM_PIDS[$i]}" 2>/dev/null ||
      room_fail "DEAD: $(room_name "$i")" "$(room_name "$i")"
  done
  return 0
}

# room_kill_all: clean-quit every still-tracked instance (kill_pair's
# SIGTERM + SIGKILL fallback)
room_kill_all() {
  local pids="" i
  for i in "${!ROOM_PIDS[@]}"; do
    [ -n "${ROOM_PIDS[$i]}" ] && pids="$pids ${ROOM_PIDS[$i]}"
  done
  [ -n "$pids" ] && kill_pair $pids
  return 0
}

# fly_all SECS: everyone holds thrust + fire for SECS, then releases
fly_all() {
  local secs=$1 w
  for w in "${ROOM_WINS[@]}"; do
    [ -n "$w" ] && { xdotool keydown --window "$w" space
                     xdotool keydown --window "$w" w; }
  done
  sleep "$secs"
  for w in "${ROOM_WINS[@]}"; do
    [ -n "$w" ] && { xdotool keyup --window "$w" space
                     xdotool keyup --window "$w" w; }
  done
  return 0
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
