---
name: headless-testing
description: Drive Newtonia end-to-end under Xvfb with synthesized xdotool input — headless runtime testing on Linux with no display. Use when verifying gameplay flows, state transitions, or object-lifetime crashes at runtime, capturing screenshots, or getting gdb backtraces from a headless run.
---

# Headless runtime testing & debugging (Linux, no display)
**See TESTING.md for the full test inventory** — build gates, in-binary
selftests (`NEWTONIA_NET_SELFTEST`, `NEWTONIA_REPLAY_SELFTEST`),
signal-worker tests, the committed netplay e2e drivers (`test/e2e/`), and the
`STEAM_BUILD` stub check. This
section documents the underlying driver technique those drivers use.

A clean build is not proof that gameplay flows work — state transitions, input
handling, and object lifetimes (double-delete, use-after-free across state
swaps) only fail at runtime. The game runs fine under Xvfb with software GL,
so it can be driven end-to-end with synthesized input and screenshotted:

```sh
sudo apt-get install -y xvfb xdotool x11-apps imagemagick gdb
```

**Pattern** — write a driver script that launches the game, sends keys, and
checks liveness after every step, then run it under `xvfb-run`:
```sh
export SDL_AUDIODRIVER=dummy       # no sound device in the container
./newtonia > game.log 2>&1 &  PID=$!
sleep 3
W=$(xdotool search --name . | tail -1)          # the game window id
xdotool key --window $W Return                  # dismiss attract screen
xdotool key --window $W Return                  # select NEW GAME
xdotool key --window $W n                       # skip level (see gotcha below)
kill -0 $PID || { wait $PID; echo "CRASHED status=$?"; }   # 139 = SIGSEGV
xwd -id $W -out shot.xwd && convert shot.xwd shot.png      # screenshot
```
```sh
xvfb-run -a -s "-screen 0 1280x800x24" ./drive.sh
```
Insert a `kill -0 $PID` liveness check plus screenshot after **each** input so
a crash is pinpointed to the step that caused it. Inspect the PNGs to confirm
the right screen is showing (menu vs game vs intro), not just that the process
survived.

**Backtrace on crash** — rebuild with symbols (override `CFLAGS`, keeping the
SDL flags and `-MMD -MP`) and run the same driver with the game wrapped in
batch-mode gdb:
```sh
make clean && make -j CFLAGS="-Wall -g -O0 -std=c++11 $(sdl2-config --cflags) -MMD -MP"
gdb -batch -ex run -ex "bt 20" --args ./newtonia > gdb.log 2>&1 &
# ...send the same xdotool keys, then read gdb.log for the stack trace
```

**Gotchas learned the hard way:**
- Screenshot the game window with `xwd -id $W` + `convert`; `import -window
  root` captures black under Xvfb once the GL window is gone (and a black
  root capture usually means the game already crashed).
- The skip-level key (`n`) sets `time_until_next_generation = 0`, so the next
  generation (and its intro screen) starts **immediately** — there is no 5 s
  tick countdown like a normally cleared level. Time waits in driver scripts
  accordingly. It also works **on** an intro screen (skips that level and
  dismisses the intro in one press), so level-marching scripts — and
  `adb shell input keyevent KEYCODE_N` on device — can hammer `n` without
  stalling on intro generations.
- `kill $PID; wait $PID` reports 0 for a healthy shutdown (SDL turns SIGTERM
  into an SDL_QUIT event and the desktop loop exits cleanly through the same
  path as Alt-F4 — saves included); 139 means the game segfaulted on its own.
- `xdotool` prints `XGetInputFocus returned the focused window of 1` warnings
  under Xvfb; they are harmless — filter with `grep -v XGetInputFocus`.
- Give the driver a hard `timeout` and log to a file; a hung X client can
  otherwise keep `xvfb-run` alive forever.
- Late-game testing: `NEWTONIA_BETA=1 NEWTONIA_START_GENERATION=N ./newtonia`
  starts a new game at generation N (world size + hazards included). Flagged
  as a cheat, so achievements stay suppressed for that game.
- Weapon testing: `NEWTONIA_ALL_WEAPONS=1 ./newtonia` grants every player the
  full arsenal (all primary gun variants + all secondaries) at 999 rounds,
  re-granted on each respawn (`Ship::give_all_weapons`). A numeric value > 1
  sets the rounds instead (`NEWTONIA_ALL_WEAPONS=30` — quick drain tests for
  the low-ammo warning / exhausted-weapon switch). God mode is excluded
  (it hijacks the primary slot). Flagged as a cheat, so achievements stay
  suppressed for that game. The grant only fires when the arsenal is bare
  (base gun only, no secondaries) — a RESUMED save's ship never qualifies, so
  test from a fresh NEW GAME (mind the "New game?" confirm: NO is the
  default).

