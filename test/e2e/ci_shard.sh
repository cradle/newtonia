#!/bin/bash
# Run one CI shard of the e2e suite (TESTING.md section 4). The shard lists
# live here, not in the workflow, so the same split runs locally:
#
#   test/e2e/ci_shard.sh netplay-core        # one shard
#   test/e2e/ci_shard.sh list                # every shard name
#   test/e2e/ci_shard.sh all                 # the whole suite, serially
#
# Needs a netplay build at the repo root (make -j) and, for every shard except
# solo-*/lan, node+npx for the local signal relay this script boots itself.
# Each driver re-execs itself under its own xvfb-run, so shards run drivers
# SEQUENTIALLY: measured 2026-08-12, four netplay drivers at once on a 4-vCPU
# runner lost a shock bolt in one of them (shock_net passes alone in 54s), so
# concurrency inside a shard buys wall-clock at the cost of exactly the timing
# assertions these drivers exist to make. Parallelism belongs across shards.
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT" || exit 1

# Shards are balanced on measured wall time (2026-08-12, 4 vCPU + llvmpipe):
# ~9-11 min each, so the whole suite lands in about the time of one shard.
# turnexpiry.sh is deliberately absent — it needs real Cloudflare TURN
# credentials and UDP egress, so no runner can host it (TESTING.md).
SHARDS="solo-replay solo-misc leaderboard lobby-and-lan netplay-core netplay-resilience seats-and-soak"

shard_drivers() {
  case "$1" in
    solo-replay)  echo "replay_keyframe replay_menu fourplayer replay replay_playback" ;;
    solo-misc)    echo "lan replay_failures video identity_attested identity_tick" ;;
    # leaderboard.sh alone: it stands up its own board worker and plays six
    # scenarios through it, two of them needing a dedicated worker of their
    # own, and its S1 scoring spray retries until a fresh world actually
    # scores — measured 524 s, 735 s, 886 s and once past 900 s on the same
    # code. Sharing a shard meant its variance decided that shard's fate;
    # alone it costs only its own wall clock, in parallel with the rest.
    leaderboard)  echo "leaderboard" ;;
    lobby-and-lan) echo "lan_hidden lanclip lanrejoin lanrename byecard lankeep
                         ownroom mismatch policy identity identity_legacy pairstart fly
                         nseat_lobby_kick nseat_lobby_mouse" ;;
    netplay-core) echo "room weapons_net missile_net shock_net shock_hazards_net
                        hazards_net timeslow_net impacts replay_online" ;;
    netplay-resilience) echo "rejoin rejoinexit hiccup blackout invite hostresume
                              spectate spectate_disconnect revive" ;;
    seats-and-soak) echo "nseat nseat_rejoin nseat_gameover nseat_swap nseat_soak
                          gensoak nseat_kick nseat_anon" ;;
    *) return 1 ;;
  esac
}

# Every driver in test/e2e/ must appear in a shard above, or be named here
# with the reason it deliberately is not. This is the inverse of run_shard's
# MISSING check, and it exists because the gap it closes actually happened:
# the four kick/ban drivers shipped present in the tree but listed in no
# shard, so no CI job ever ran them.
#   turnexpiry: real Cloudflare TURN credentials + UDP egress (see above)
#   threeseat / threeseat_rejoin / fourseat: wrappers that re-run the nseat
#     drivers with a different SEATS (see driver_env below)
UNSHARDED="turnexpiry threeseat threeseat_rejoin fourseat"

check_shard_coverage() {
  local s d name all missing=""
  all=" $(for s in $SHARDS; do shard_drivers "$s"; done | tr '\n' ' ') $UNSHARDED "
  for d in "$ROOT"/test/e2e/*.sh; do
    name=$(basename "$d" .sh)
    case "$name" in lib|ci_shard) continue ;; esac
    case "$all" in *" $name "*) ;; *) missing="$missing $name" ;; esac
  done
  [ -z "$missing" ] || {
    echo "FATAL: driver(s) in no shard:$missing"
    echo "       add each to a shard list or to UNSHARDED with a reason"
    exit 1
  }
}

# Shards whose drivers need the shared local relay. The solo shards do not:
# their drivers either never connect, drive the LAN door with a deliberately
# dead signal URL, or — leaderboard.sh, identity_attested.sh, identity_tick.sh
# — stand up their own worker on their own port with its own flags.
shard_needs_relay() {
  case "$1" in
    solo-replay|solo-misc|leaderboard) return 1 ;;
    *) return 0 ;;
  esac
}

# N-seat drivers default to 4 seats here (FOURPLAYER.md B6); the wrappers
# threeseat.sh / threeseat_rejoin.sh / fourseat.sh only re-run these with a
# different SEATS, so CI runs the drivers directly.
driver_env() {
  case "$1" in
    nseat|nseat_rejoin|nseat_gameover|nseat_swap|nseat_soak) echo "SEATS=4" ;;
    *) echo "" ;;
  esac
}

RELAY_PID=""
# workerd/wrangler processes already running when this script started are
# someone else's — TESTING.md advertises running the shards locally, where a
# developer may have their own :8787 relay (test/e2e/lib.sh tells them to
# keep one up) or an unrelated project's wrangler. Recorded once, spared
# from every sweep below: the cleanup used to assume it owned every workerd
# on the machine and kill -9'd a developer's relay mid-shard.
PREEXISTING_WORKERS=" $(pgrep -x workerd 2>/dev/null | tr '\n' ' ')$(pgrep -f 'wrangler[-]dist' 2>/dev/null | tr '\n' ' ')"

start_relay() {
  # A PLAIN relay: identity.sh asserts that an unattested claim stays
  # unattested, so FAKE_VERIFY here would fail it (and it is the only flavour
  # difference in the suite — the attested drivers boot their own).
  # The rate limits are the reason for the overrides: wrangler dev sees no
  # CF-Connecting-IP, so every socket shares the key "local" and production's
  # 10 host-creates / 10 min would refuse a shard's later drivers as
  # "rate-limited" — which reads exactly like a protocol bug.
  #
  # Refuse a squatted port instead of adopting it: the readiness probe below
  # only greps for newtonia-signal, so a developer's own :8787 relay would
  # pass it while OUR npx died on the bind — the shard then ran on a relay
  # of the wrong flavour (FAKE_VERIFY fails identity.sh; production limits
  # read as protocol bugs) and the cleanup later killed it out from under
  # them. The /dev/tcp probe answers "is anything listening", response or no.
  if (exec 3<>/dev/tcp/127.0.0.1/8787) 2>/dev/null; then
    echo "FATAL: something else is already listening on :8787 — stop it first"
    echo "       (this script boots and owns its own relay)"
    exit 1
  fi
  echo "== starting local signal relay on :8787"
  ( cd "$ROOT/signal" && rm -rf .wrangler &&
    exec npx wrangler@4 dev --local --port 8787 \
      --var RATE_HOST_LIMIT:500 --var RATE_JOIN_LIMIT:1000 ) > "$OUTDIR/relay.log" 2>&1 &
  RELAY_PID=$!
  local i
  for i in $(seq 1 90); do
    curl -s --max-time 2 http://127.0.0.1:8787/ | grep -q newtonia-signal && break
    sleep 1
  done
  curl -s --max-time 2 http://127.0.0.1:8787/ | grep -q newtonia-signal || {
    echo "FATAL: relay never came up"; tail -20 "$OUTDIR/relay.log"; exit 1; }
}

stop_relay() {
  [ -n "$RELAY_PID" ] || return 0
  # Killing the npx wrapper leaves workerd holding :8787, and the next boot
  # then dies with "Address already in use" — take the whole tree down.
  # OUR tree only (relay_tree), not a blanket `pkill -x workerd`: a locally
  # running developer's workerd is not ours to kill (PREEXISTING_WORKERS).
  # TERM first, then KILL: a wedged workerd that shrugs off the TERM would
  # keep the socket through ensure_relay's restart, and the new relay would
  # die on the bind exactly like the un-killed case this function fixes.
  local tree p
  tree=$(relay_tree)
  for p in $tree; do kill "$p" 2>/dev/null; done
  sleep 1
  for p in $tree; do kill -9 "$p" 2>/dev/null; done
  RELAY_PID=""
}
trap stop_relay EXIT

# xdotool prints "XGetInputFocus returned the focused window of 1" for every
# single keystroke, so an unfiltered tail of a driver log is 25 lines of that
# and none of the verdict. Everything printed back to the job log goes through
# this — the first CI failure was undiagnosable for exactly that reason.
driver_log() { grep -av "XGetInputFocus" "$1"; }

# The driver's own verdict lines: what it was doing and why it gave up.
driver_reason() { driver_log "$1" | grep -aE "FAIL|FATAL|DEAD|MISSING|NO " | head -3; }

# Kill workers a DRIVER stood up for itself, leaving this shard's relay alone.
#
# leaderboard.sh, identity_attested.sh and identity_tick.sh each boot their own
# worker on their own port, and killing the npx wrapper leaves workerd holding
# it. A retry then cannot bind, silently talks to the PREVIOUS attempt's
# worker, and inherits its database: the retry of leaderboard.sh found the
# first attempt's rows already on the board, so its fresh score could not
# place and S1 failed with "never placed" — the retry poisoned by the very
# attempt it was meant to redo (2026-08-12). The shard relay is identified by
# its port and deliberately spared; everything else wrangler-shaped goes.
# The relay is spared by PROCESS TREE, not by matching its port in a command
# line: `wrangler dev` spawns TWO workerd processes and only one of them
# carries --socket-addr=...:8787, so a port match killed the other and took
# the relay down with it — every driver after the first cleanup then failed
# with "no signal relay" (2026-08-12, the fix's own first CI run).
relay_tree() {
  [ -n "$RELAY_PID" ] || return 0
  local out="$RELAY_PID" frontier="$RELAY_PID" next p c
  while [ -n "$frontier" ]; do
    next=""
    for p in $frontier; do
      for c in $(pgrep -P "$p" 2>/dev/null); do next="$next $c"; out="$out $c"; done
    done
    frontier="$next"
  done
  echo "$out"
}

kill_driver_workers() {
  local p spare
  # Spare the shard relay's tree AND anything that predates this script —
  # only what a driver stood up mid-shard is ours to sweep.
  spare=" $(relay_tree) $PREEXISTING_WORKERS "
  for p in $(pgrep -x workerd 2>/dev/null) $(pgrep -f 'wrangler[-]dist' 2>/dev/null); do
    case "$spare" in *" $p "*) continue ;; esac
    kill -9 "$p" 2>/dev/null
  done
  return 0
}

# ...and if the relay went down anyway — killed here, crashed, or wedged — put
# it back rather than letting every remaining driver in the shard fail with
# "no signal relay". Cheap: one HTTP probe between attempts.
ensure_relay() {
  shard_needs_relay "$CURRENT_SHARD" || return 0
  [ -n "$RELAY_PID" ] || return 0
  curl -s --max-time 2 http://127.0.0.1:8787/ | grep -q newtonia-signal && return 0
  echo "  (relay is gone — restarting it)"
  # Tear the old tree down FIRST. Two of the three cases this function
  # exists for — wedged-but-listening, and healthy but slow past one 2 s
  # probe on a loaded runner — leave the old workerd holding :8787, and
  # start_relay cannot bind through it: clearing RELAY_PID and restarting
  # used to burn the 90 s wait, FATAL, and exit the whole shard mid-list,
  # turning one missed probe into every remaining driver unrun.
  stop_relay
  start_relay
}

# A driver gets one retry: these drive real windows through a software GL
# stack, and a dropped keystroke is not a product regression. A driver that
# needs the retry is reported WITH the reason its first attempt failed, so a
# creeping flake stays visible — and diagnosable — instead of being laundered
# by the green tick.
# Drivers that do NOT get a retry: a second attempt would double a run already
# measured past 15 minutes, and this one's failures have so far been its own
# leftover state rather than the dropped keystroke a retry exists for.
driver_attempts() {
  case "$1" in
    leaderboard) echo 1 ;;
    *)           echo 2 ;;
  esac
}

run_driver() {
  local d=$1 attempt rc start elapsed last
  last=$(driver_attempts "$d")
  for attempt in $(seq 1 "$last"); do
    start=$(date +%s)
    ( export NEWTONIA_TEST_OUT="$OUTDIR/$d.$attempt"
      mkdir -p "$NEWTONIA_TEST_OUT"
      env $(driver_env "$d") timeout "$(driver_timeout "$d")" \
        bash "$ROOT/test/e2e/$d.sh" ) > "$OUTDIR/$d.attempt$attempt.log" 2>&1
    rc=$?
    elapsed=$(( $(date +%s) - start ))
    if [ "$rc" = 0 ]; then
      if [ "$attempt" = 1 ]; then
        printf '  ok     %-22s %4ss\n' "$d" "$elapsed"
      else
        printf '  FLAKY  %-22s %4ss (passed on retry)\n' "$d" "$elapsed"
        driver_reason "$OUTDIR/$d.attempt1.log" | sed 's/^/           first attempt: /'
        FLAKY="$FLAKY $d"
      fi
      return 0
    fi
    # 124 is timeout(1) killing a wedged driver — worth naming, since its log
    # ends mid-step rather than with a verdict.
    [ "$rc" = 124 ] && echo "  .. $d TIMED OUT after $(driver_timeout "$d")s"
    [ "$attempt" = "$last" ] || echo "  .. $d failed in ${elapsed}s (rc=$rc), retrying"
    kill_driver_workers   # a leftover worker would serve the retry stale state
    ensure_relay          # ...and the cleanup must not cost the shard its relay
  done
  printf '  FAIL   %-22s %4ss (rc=%s)\n' "$d" "$elapsed" "$rc"
  [ "$last" = 1 ] || driver_reason "$OUTDIR/$d.attempt1.log" |
    sed 's/^/         first attempt: /'
  echo "  ---- $d, last attempt (xdotool noise filtered) ----"
  driver_log "$OUTDIR/$d.attempt$last.log" | tail -40 | sed 's/^/  | /'
  echo "  ----"
  FAILED="$FAILED $d"
  return 1
}

CURRENT_SHARD=""
run_shard() {
  local shard=$1 d drivers
  CURRENT_SHARD="$shard"
  drivers=$(shard_drivers "$shard") || { echo "unknown shard: $shard"; exit 2; }
  echo "== shard $shard"
  shard_needs_relay "$shard" && [ -z "$RELAY_PID" ] && start_relay
  for d in $drivers; do
    [ -f "$ROOT/test/e2e/$d.sh" ] || { echo "  MISSING $d.sh"; FAILED="$FAILED $d"; continue; }
    run_driver "$d"
  done
}

# Per-driver cap. Most drivers land under 150 s and the longest of the ordinary
# ones is replay_playback at ~300 s, so 480 leaves headroom while keeping a
# wedged driver from eating the shard: at 900 s a single driver could burn 30
# minutes across its two attempts, overrun the JOB timeout, and take the shard
# down with no summary and no uploaded artifact (solo-misc, run 1).
DRIVER_TIMEOUT="${DRIVER_TIMEOUT:-480}"

# ...except the ones that are genuinely long. leaderboard.sh stands up its own
# board worker and plays six scenarios through it, including two that need a
# dedicated worker of their own: 8m44s measured locally, all six passing. It
# spent two attempts hitting the 480 s cap and reporting a timeout for work
# that was simply not finished (2026-08-12).
driver_timeout() {
  case "$1" in
    leaderboard) echo 1500 ;;
    *)           echo "$DRIVER_TIMEOUT" ;;
  esac
}
OUTDIR="${NEWTONIA_CI_OUT:-$(mktemp -d /tmp/newtonia-ci.XXXXXX)}"
mkdir -p "$OUTDIR"
FAILED=""
FLAKY=""

case "${1:-}" in
  ""|-h|--help) echo "usage: $0 <shard|all|list>"; echo "shards: $SHARDS"; exit 2 ;;
  list)         echo "$SHARDS"; exit 0 ;;
  all)          [ -x "$ROOT/newtonia" ] || { echo "FATAL: build first (make -j)"; exit 1; }
                check_shard_coverage
                echo "logs: $OUTDIR"
                for s in $SHARDS; do run_shard "$s"; done ;;
  *)            [ -x "$ROOT/newtonia" ] || { echo "FATAL: build first (make -j)"; exit 1; }
                check_shard_coverage
                echo "logs: $OUTDIR"
                run_shard "$1" ;;
esac

stop_relay
echo
[ -n "$FLAKY" ] && echo "PASSED ON RETRY:$FLAKY"
if [ -n "$FAILED" ]; then
  echo "E2E-SHARD-FAIL:$FAILED"
  exit 1
fi
echo "E2E-SHARD-OK"
