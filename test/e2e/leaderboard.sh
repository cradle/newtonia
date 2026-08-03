#!/bin/bash
# LEADERBOARD.md L2 exit criteria, headless, against a local board worker
# (wrangler dev --local with FAKE_VERIFY — no Cloudflare account involved):
#   S1  a clean personal best + game over -> the qualify runs, the prompt
#       appears, YES uploads best.nrp, the worker places it and the row is
#       readable back via `top` with the header's exact score
#   S2  worker unreachable -> the qualify degrades silently (no prompt, no
#       error card, the game stays alive)
#   S3  no best promotion (cheat-flagged only run) -> no board traffic
#   S4  leaderboard_prompts=0 -> AUTO-upload (no prompt, same status text)
#   S5  consumed credential (REJECT_FIRST_VERIFY) -> the retry peek-polls
#       and resubmits a provably DIFFERENT credential, and places
#   S6  mint not landed at submit (TEST_CRED_DELAY) -> empty cred rejected,
#       the retry waits for the mint and places
#
# The prompt's candidate is best.nrp, promoted by a CLEAN run — so S1/S4
# score a clean run first (spray shots), abandon, and NEW GAME to rotate it
# into best (arming the game-over offer, which deliberately survives to the
# next game over). The game-over run itself may then use the time-speed
# cheat to die fast by overheat: the cheat flags THAT run, but the offer
# and the upload belong to the clean best.
set -u
if [ -z "${DISPLAY:-}" ]; then
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
. "$(dirname "$0")/lib.sh"
export NEWTONIA_REPLAY_ENABLE=1
export NEWTONIA_NET_NAME=E2E   # FAKE_VERIFY derives the account from this
# The plain build has no verify backend, so the upload UI is gated off
# (LEADERBOARD.md: no unattested submissions). This dev hook forces it on
# and sends a dummy credential the FAKE_VERIFY worker attests.
export NEWTONIA_BOARD_TEST_CRED=e2e-cred

FAIL=0
fail() { echo "FAIL: $*"; FAIL=1; }

# ---- board worker: use NEWTONIA_BOARD_URL if provided, else start one ----
WRANGLER_PID=""
if [ -z "${NEWTONIA_BOARD_URL:-}" ]; then
  export NEWTONIA_BOARD_URL="ws://127.0.0.1:8799/board"
  ( cd "$ROOT/board" &&
    npx -y wrangler@4 dev --local --port 8799 \
      --var FAKE_VERIFY:1 --var SUBMIT_LIMIT:100 --var CANONICAL_SEASONS_ONLY:0 \
      > "$OUT/wrangler.log" 2>&1 ) &
  WRANGLER_PID=$!
fi
BOARD_HTTP="http://${NEWTONIA_BOARD_URL#ws://}"; BOARD_HTTP="${BOARD_HTTP%/board}"
for i in $(seq 1 60); do
  curl -s --max-time 2 "$BOARD_HTTP" | grep -q newtonia-board && break
  [ "$i" = 60 ] && { echo "FATAL: no board worker at $NEWTONIA_BOARD_URL"; exit 1; }
  sleep 2
done

P=""
REJECT_PID=""   # S5's dedicated reject-first worker
trap 'kill -9 $P 2>/dev/null; [ -n "$WRANGLER_PID" ] && kill $WRANGLER_PID 2>/dev/null; [ -n "$REJECT_PID" ] && kill $REJECT_PID 2>/dev/null' EXIT

use_home() {
  export XDG_DATA_HOME="$OUT/xdg-$1"
  RDIR="$XDG_DATA_HOME/cc.gfm/newtonia/replays"
}
launch_game() { "$ROOT/newtonia" > "$OUT/$1.log" 2>&1 & echo $!; }
win() { xdotool search --name Newtonia | tail -1; }

# Wait for a log line (regex), up to $3 seconds. True if it appeared.
wait_log() {
  local i
  for i in $(seq 1 "$3"); do
    grep -aq "$2" "$1" && return 0
    kill -0 $P 2>/dev/null || { fail "game died waiting for: $2"; return 1; }
    sleep 1
  done
  return 1
}

# Score a clean run, abandon it, then NEW GAME: the rotation promotes
# best.nrp and arms the game-over offer. Leaves the game in a fresh run.
# The spray is probabilistic (a fresh world spawns its asteroids clear of
# the ship), so a scoreless attempt CONTINUEs the same run and sprays
# again — the abandoned header says whether anything landed.
clean_best() {
  local W=$1 attempt SC
  key "$W" Return; sleep 0.5
  key "$W" Return                              # NEW GAME (fresh home)
  for attempt in 1 2 3; do
    sleep 0.5; key "$W" space                  # spawn
    xdotool keydown --window "$W" w            # cruise into the field...
    xdotool keydown --window "$W" d            # ...spiralling...
    for i in $(seq 1 100); do xdotool key --window "$W" space; sleep 0.08; done
    xdotool keyup --window "$W" d
    xdotool keyup --window "$W" w
    sleep 1
    key "$W" Escape; sleep 2                   # clean abandon (header patched)
    SC=$(python3 "$ROOT/test/e2e/replay_check.py" "$RDIR/current.nrp" |
         sed -n 's/^score=//p')
    [ "${SC:-0}" -gt 0 ] && break
    key "$W" Return; sleep 0.5; key "$W" Return  # attract -> CONTINUE, retry
  done
  key "$W" Return; sleep 0.5                   # attract -> menu
  key "$W" s; key "$W" Return                  # NEW GAME (CONTINUE is first)
  key "$W" w; key "$W" Return                  # confirm YES (NO is default)
  sleep 1
}

# Die in the current run. The heat mechanic is disabled (ship.cpp), so the
# ship does NOT overheat — it dies by colliding with asteroids while
# thrusting. The time cheat (8x) speeds the wall-clock; held thrust drives
# it into the field. The run is cheat-flagged, which is fine: the upload
# candidate is the earlier CLEAN best.nrp, not this run.
crash_to_game_over() {
  local W=$1 LOG=$2 i
  key "$W" space                               # spawn out of the countdown
  for i in $(seq 1 7); do key "$W" equal; done # time cheat: 8x wall-clock
  xdotool keydown --window "$W" w              # thrust into the asteroids
  # Held X key state can drop under Xvfb, and a lucky heading can cruise
  # through open space for a long time — so while waiting, periodically
  # re-assert the thrust hold and nudge the heading so the wraps sweep
  # fresh parts of the field instead of retracing one lane.
  for i in $(seq 1 48); do                     # up to 240 s, nudge every 5 s
    grep -aq "replay: run ended" "$LOG" && break
    kill -0 $P 2>/dev/null || { fail "game died before game over"; break; }
    xdotool keydown --window "$W" w
    key "$W" d
    sleep 5
  done
  grep -aq "replay: run ended" "$LOG" || fail "never reached game over"
  xdotool keyup --window "$W" w
}

echo "===== S1: personal best -> prompt -> YES -> placed ====="
use_home s1
P=$(launch_game s1); sleep 2; W=$(win)
clean_best "$W"
[ -f "$RDIR/best.nrp" ] || fail "S1: clean run did not promote best.nrp"
BSCORE=$(python3 "$ROOT/test/e2e/replay_check.py" "$RDIR/best.nrp" |
         sed -n 's/^score=//p')
[ "${BSCORE:-0}" -gt 0 ] || fail "S1: scoring run scored 0 (shots missed?)"
crash_to_game_over "$W" "$OUT/s1.log"
wait_log "$OUT/s1.log" "board: qualify" 10 || fail "S1: qualify never sent"
wait_log "$OUT/s1.log" "board: would place .* prompting" 15 ||
  fail "S1: prompt never armed"
sleep 4                                        # the card's 3 s input grace
key "$W" Return                                # YES (default highlight)
wait_log "$OUT/s1.log" "board: uploading" 10 || fail "S1: YES did not upload"
wait_log "$OUT/s1.log" "board: placed #" 20 || fail "S1: never placed"
alive $P s1
# The row is readable back with the header's exact score.
SEASON=$(python3 - "$RDIR/best.nrp" <<'EOF'
import sys
h = open(sys.argv[1], "rb").read(32)
print(h[8:32].split(b"\0")[0].decode())
EOF
)
node - "$NEWTONIA_BOARD_URL" "$SEASON" "$BSCORE" <<'EOF' || fail "S1: row not on the board"
const [url, season, score] = process.argv.slice(2);
const ws = new WebSocket(url);
ws.onopen = () => ws.send(JSON.stringify({ t: "top", season, players: 1, count: 10 }));
ws.onmessage = (m) => {
  const f = JSON.parse(m.data);
  const hit = (f.rows || []).some((r) => r.score === Number(score) &&
                                         r.name === "E2E" && r.has_replay);
  console.log(hit ? "ROW OK" : "ROW MISSING " + m.data);
  process.exit(hit ? 0 : 1);
};
setTimeout(() => process.exit(1), 10000);
EOF
key "$W" Return; sleep 1                       # leave the card
kill -9 $P; wait $P 2>/dev/null; P=""; sleep 1

echo "===== S2: worker unreachable -> silent degradation ====="
use_home s2
P=$(NEWTONIA_BOARD_URL="ws://127.0.0.1:9/board" launch_game s2)
sleep 2; W=$(win)
clean_best "$W"
crash_to_game_over "$W" "$OUT/s2.log"
wait_log "$OUT/s2.log" "board: qualify" 10 || fail "S2: qualify never attempted"
sleep 6                                        # closed/timeout window
grep -aq "board: would place" "$OUT/s2.log" && fail "S2: prompt on a dead worker"
grep -aq "board: connection closed\|board: qualify timed out" "$OUT/s2.log" ||
  fail "S2: no silent teardown logged"
alive $P s2
kill -9 $P; wait $P 2>/dev/null; P=""; sleep 1

echo "===== S3: no personal best -> no board traffic ====="
use_home s3
P=$(launch_game s3); sleep 2; W=$(win)
key "$W" Return; sleep 0.5; key "$W" Return    # NEW GAME, no clean best first
crash_to_game_over "$W" "$OUT/s3.log"          # cheat flags this run: no promotion
sleep 2
grep -aq "board:" "$OUT/s3.log" && fail "S3: board traffic without a personal best"
alive $P s3
kill -9 $P; wait $P 2>/dev/null; P=""; sleep 1

echo "===== S4: leaderboard_prompts=0 -> AUTO-upload, no prompt ====="
# The setting picks ask-vs-auto, never "don't upload": with prompts off a
# qualifying best skips the question and uploads straight away, showing
# the same status text (decided 2026-08-03).
use_home s4
mkdir -p "$XDG_DATA_HOME/cc.gfm/newtonia"
echo "leaderboard_prompts=0" > "$XDG_DATA_HOME/cc.gfm/newtonia/preferences.ini"
P=$(launch_game s4); sleep 2; W=$(win)
clean_best "$W"
crash_to_game_over "$W" "$OUT/s4.log"
for i in $(seq 1 20); do
  grep -aq "board: placed" "$OUT/s4.log" && break; sleep 1
done
grep -aq "auto-uploading" "$OUT/s4.log" || fail "S4: no auto-upload with prompts off"
grep -aq "board: placed" "$OUT/s4.log" || fail "S4: auto-upload never placed"
grep -aq " - prompting" "$OUT/s4.log" && fail "S4: prompt shown with prompts off"
alive $P s4
kill -9 $P; wait $P 2>/dev/null; P=""

echo "===== S5: consumed credential -> retry submits a FRESH one ====="
# Credential-lifecycle hardening, single-use case: the verify credential is
# minted async and re-minted per read, so a submit can present an
# already-consumed value (worker answers "unverified"). The client must
# poll peek (no re-mint) until a DIFFERENT credential lands, then resubmit
# ONCE. A dedicated worker with REJECT_FIRST_VERIFY rejects the first
# submit per connection to model the consumed ticket; the varying test
# credential ("<base>-<gen>") lets the worker log prove the resubmit
# carried a genuinely different value.
REJECT_URL="ws://127.0.0.1:8796/board"
( cd "$ROOT/board" && npx -y wrangler@4 dev --local --port 8796 \
    --var FAKE_VERIFY:1 --var SUBMIT_LIMIT:100 --var CANONICAL_SEASONS_ONLY:0 --var REJECT_FIRST_VERIFY:1 \
    > "$OUT/wrangler-reject.log" 2>&1 ) &
REJECT_PID=$!
for i in $(seq 1 60); do
  curl -s --max-time 2 "http://127.0.0.1:8796" | grep -q newtonia-board && break
  [ "$i" = 60 ] && fail "S5: reject worker never came up"
  sleep 2
done
# Distinct name = distinct FAKE_VERIFY account: the reject worker shares
# the main worker's local D1 (both `wrangler dev --local` from board/), so
# submitting as S1's account would race its score into a "not-best"
# refusal whenever S5's random spray scored lower.
use_home s5
P=$(NEWTONIA_NET_NAME=E2ES5 NEWTONIA_BOARD_URL="$REJECT_URL" \
    launch_game s5); sleep 2; W=$(win)
clean_best "$W"
crash_to_game_over "$W" "$OUT/s5.log"
wait_log "$OUT/s5.log" "board: would place" 15 || fail "S5: no prompt"
sleep 4
key "$W" Return                                  # YES
wait_log "$OUT/s5.log" "board: upload unverified - waiting" 10 ||
  fail "S5: first submit not rejected"
wait_log "$OUT/s5.log" "board: retrying upload with a fresh credential" 10 ||
  fail "S5: no retry after unverified"
wait_log "$OUT/s5.log" "board: placed #" 25 || fail "S5: retry did not place"
# The worker saw BOTH generations: -1 rejected, -2 accepted (distinct).
grep -aq "cred=e2e-cred-1" "$OUT/wrangler-reject.log" ||
  fail "S5: first submit's credential not seen by the worker"
grep -aq "cred=e2e-cred-2" "$OUT/wrangler-reject.log" ||
  fail "S5: retry did not carry a fresh (different) credential"
alive $P s5
kill -9 $P; wait $P 2>/dev/null; P=""
kill $REJECT_PID 2>/dev/null; REJECT_PID=""

echo "===== S6: mint not landed at submit -> retry waits for it ====="
# Credential-lifecycle hardening, empty case: the warm's async mint has not
# landed when the player answers YES, so the submit carries an EMPTY
# credential, which the worker rejects (FAKE_VERIFY still requires a
# non-empty cred, like every real backend). The retry peek-polls until the
# mint lands (NEWTONIA_BOARD_TEST_CRED_DELAY=3 reads), then resubmits.
# Runs against the MAIN worker — no reject var needed; the rejection is the
# empty credential itself. A distinct cred base isolates its worker log, and
# a distinct NAME keeps it a separate FAKE_VERIFY account: S1 already placed
# a (higher) score for "E2E" on this shared board, and a same-account lower
# score is correctly refused "not-best" — not what S6 is probing.
use_home s6
P=$(NEWTONIA_NET_NAME=E2ES6 NEWTONIA_BOARD_TEST_CRED=e2e-c6 \
    NEWTONIA_BOARD_TEST_CRED_DELAY=3 launch_game s6); sleep 2; W=$(win)
clean_best "$W"
crash_to_game_over "$W" "$OUT/s6.log"
wait_log "$OUT/s6.log" "board: would place" 15 || fail "S6: no prompt"
sleep 4
key "$W" Return                                  # YES
wait_log "$OUT/s6.log" "board: upload unverified - waiting" 10 ||
  fail "S6: empty-credential submit not rejected"
wait_log "$OUT/s6.log" "board: retrying upload with a fresh credential" 10 ||
  fail "S6: no retry once the mint landed"
wait_log "$OUT/s6.log" "board: placed #" 25 || fail "S6: retry did not place"
grep -aq "cred=e2e-c6-1" "$OUT/wrangler.log" ||
  fail "S6: retry did not carry the landed credential"
alive $P s6
kill -9 $P; wait $P 2>/dev/null; P=""

assert_clean "$OUT"/s1.log "$OUT"/s2.log "$OUT"/s3.log "$OUT"/s4.log \
             "$OUT"/s5.log "$OUT"/s6.log
[ "$FAIL" = 0 ] && echo "ALL PASS" || echo "FAILURES"
exit $FAIL
