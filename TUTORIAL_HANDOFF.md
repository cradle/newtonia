# Tutorial handoff

Written 2026-09-21 at the end of the session that built the new-player
start screen and the first-flight tutorial (PR #566, shipped in v1.64.0).
PR #570 (v1.64.1) reworked the start screen, and the 2026-09-24 review
fixes followed; the sections below are updated for both. CLAUDE.md's
Menu and **Tutorial** bullets are the authoritative design notes.

## What shipped on the branch

- **Start screen** (`menu.h/cpp`): while `Preferences::tutorial_done` is
  false and no save exists, the main menu gains a TUTORIAL row on top and
  shows a one-line nudge in the high score slot. NEW GAME, ONLINE,
  OPTIONS and LEADERBOARD stay. STATS appears once `stats.dat` or a high
  score says the pilot has played, and REPLAYS once a replay exists, so an
  online-only pilot (ONLINE never latches the tutorial) still gets both.
  NEW GAME from the start screen latches the pref (it is the skip).
- **Tutorial** (`tutorial.h/cpp`, owned by `GLGame::tutorial_`, a `friend`):
  a step machine inside an ordinary offline `GLGame` with an empty field.
  Steps, in order: INPUT and HAND (touch only), LAUNCH, TURN, THRUST,
  CAMERA (desktop and pad only), FIRE, BOOST, SECONDARY, WRAP, DONE.
  `tutorial.h`'s header comment describes each step; CLAUDE.md's
  **Tutorial** bullet is the authoritative design note.
- **Prompts** (INPUT, HAND, CAMERA): one card, one `prompt_row(i, n)`
  geometry for draw AND tap (TapBand rule), `prompt_pick(row)` routing
  by step, `-1` = back = keep what is set. INPUT picks ONE HAND / TWO
  HANDS; HAND picks LEFT / CENTRE / RIGHT (one hand) or LEFT / RIGHT (two
  hands, where CENTRE and RIGHT are one arrangement). Both write their
  pref, save, and apply through `touch_layout_prefs_changed()` — the ONE
  apply site, shared with Options and the touch help card.
- **Cards**: translucent black panels outlined in a Typer-weight stroke
  (`draw_card`). Text scale (`text_scale()`): 2x in portrait, 1.5x on
  landscape touch, 1x otherwise. Long banner lines wrap at the " - "
  seam nearest the middle (`wrap_line`). On touch the banner drops under
  the pause circle (always in portrait). No GOOD interstitial: the next
  step's title lands at once, the pickup chime is the completion cue.
- **Persistence gates**: every path is cut on `GLGame::in_tutorial()`
  (saves, high score, stats, replay recording, level clear, intros,
  leaderboard prompt, P2 joins, touch help auto-show). The ctor marks
  the run suppressed via the achievements cheat latch, so nothing banks.
- **Pref**: `tutorial_done` — struct default `true` (an INI that predates
  the key belongs to a veteran), `false` only from `first_launch_defaults`.
  `NEWTONIA_TUTORIAL=0/1` overrides the reading without touching the INI.
- **Tests/docs**: `test/e2e/tutorial.sh` (desktop path, in the
  solo-replay shard of `ci_shard.sh`), `lib.sh` + `fourplayer.sh` +
  `join_link_scenarios.py` export `NEWTONIA_TUTORIAL=0`, TESTING.md entry,
  headless-testing skill gotcha, CLAUDE.md (Menu, Tutorial, Preferences).

## What the maintainer has and has not tested

- Desktop keyboard: tested by the maintainer in the field; several rounds
  of copy and layout feedback are already folded in.
- Mobile: tested in the field on a phone (the text-size and beacon
  reports came from there), BUT the INPUT and HAND prompts landed after
  that test and have only been verified headlessly.
- Controller: untested. The banner names pad buttons through
  `GLShip::pad_hint` (chosen by `using_pad()`), the CAMERA prompt answers
  the pad through `State::nav_key_from_controller`, and there is no pad
  twin of the beta skip key (a pad pilot skips via the red beacon only).

## Review follow-up

The takeover review of `bc45d976` fixes these issues on this PR:

- Native/web finger-up could answer INPUT and HAND with one tap. Prompts
  now consume the tap's synthesized key release and require fresh keyboard
  presses; touch fire/zoom/joystick routes are disabled while a card is open.
- Pause input and drawing take priority over tutorial cards. Unseated
  controllers cannot answer a prompt for another pilot.
- Remaining practice rocks are destroyed before constructing the real
  game, so their later teardown cannot subtract from the new game's global
  asteroid count (three leftovers cleared level 1).
- Practice targets spawn beside the flight path, fixing the coasting
  collision below on touch and desktop.
- BOOST and SECONDARY require actions during their own lessons. Previous
  cooldowns/fired-weapon flags cannot skip them. Secondary wording covers
  other weapons collected from practice rocks too.

`test/unit/tutorial.sh` runs real game integration regressions for these
paths and is a Linux CI gate. The e2e driver now checks both occurrences of
the repeated camera calibration steps; preference tests cover tutorial
migration and persistence.

A second review of `dfc28ca7` reproduced a remaining touch release leak:
the centre pause zone sends Enter down but P up. Choosing the final HAND
row closed the card before P arrived, immediately pausing LAUNCH. A consumed
prompt tap now swallows its trailing release across the step transition.
A fresh key-down clears that guard so a layout change that discards the
legacy release cannot swallow the next deliberate pause. Regression tests
cover both cases and both touch layouts.

## Review fixes (2026-09-24)

- WRAP and DONE ignore fire presses for their first second
  (`CARD_ARM_MS`), and the INPUT/HAND/CAMERA prompts ignore answers for
  their first half second (`PROMPT_ARM_MS`), so a pilot still shooting
  fragments can no longer skip the last cards unread.
- A pad on a tutorial started from the keyboard, mouse or touchscreen
  adopts seat 1 on its first press (`GLGame::controller`).
- Web: while a prompt is up, every finger on the HTML joystick zone or a
  circle button becomes a canvas tap (`setPromptOpen`), so the whole row
  answers in both layouts.
- The skip beacon and practice rocks are capped to the pilot's visible
  half-extent (`view_half_min`), so both are on screen on portrait phones.
- Banners use pad wording for a pad pilot on a phone; rich presence reports
  the menu during the tutorial.

## Original coasting issue (addressed by review)

On touch, FIRE now follows THRUST directly (no CAMERA freeze), so the
three practice rocks spawn dead ahead of a ship still coasting from the
thrust run; in a headless run the ship rammed them before firing. The
desktop CAMERA prompt freezes simulation but preserves momentum, so that
path is affected after dismissing the card too. Review moved the targets
off the flight path in `spawn_practice_asteroids`.

## How to verify

Build (netless is enough for the tutorial):

```sh
make NETPLAY=0 -j8
g++ -std=c++11 -fsyntax-only -Wall -I. -I/usr/include/SDL2 tutorial.cpp
```

Desktop e2e (prints `TUTORIAL-E2E-OK`; walks the CAMERA prompt for real):

```sh
bash test/e2e/tutorial.sh
```

Touch renders: there is no committed driver for the touch path. This
session used ad-hoc scripts built on `test/e2e/lib.sh`; the recipe is:

```sh
# portrait phone: Xvfb 600x1300, forced touch, a pre-written INI
xvfb-run -a -s "-screen 0 600x1300x24" bash -c '
  . test/e2e/lib.sh; unset NEWTONIA_TUTORIAL
  D="$XDG_DATA_HOME/cc.gfm/newtonia"; mkdir -p "$D"
  printf "window_width=540\nwindow_height=1170\ntutorial_done=0\n" > "$D/preferences.ini"
  P=$(launch tut NEWTONIA_BETA=1 NEWTONIA_FORCE_TOUCH=1); sleep 3
  W=$(newtonia_windows | tail -1)
  key $W Return; sleep 1; key $W Return; sleep 2   # attract -> start screen -> TUTORIAL
  shot $W input_prompt                              # INPUT card
  key $W w; key $W Return; sleep 1; shot $W hand_prompt   # ONE HAND -> HAND card (3 rows)
  key $W s; key $W Return; sleep 1; shot $W welcome       # RIGHT -> WELCOME
  key $W space; sleep 1.5; shot $W turn
  key $W n; sleep 0.5; xdotool keydown --window $W w; sleep 5; xdotool keyup --window $W w
  sleep 1; shot $W fire                              # THRUST -> FIRE, no CAMERA on touch
  kill $P; grep -a "tutorial:" "$OUT/tut.log"; echo "$OUT"'
```

Swap the screen to 1300x600 and the INI to 1170x540 for landscape. Under
`NEWTONIA_FORCE_TOUCH` the keyboard still drives the prompts (w/s move,
Return picks) and the beta `n` key skips a step. The log line
`tutorial: step NAME` marks every transition; `tutorial: input …` and
`tutorial: hand …` record the picks, and the INI shows `touch_one_hand`
and `touch_handedness` afterwards.

## Gotchas learned the hard way

- `WrappedPoint()`'s default ctor rolls a random point inside bounds
  that are not set yet when `GLGame` builds the tutorial: SIGFPE. Every
  `WrappedPoint` member is initialised `WrappedPoint(0.0f, 0.0f)`.
- Typer virtual units: landscape pins the half-height at 600 and the
  half-width grows with aspect; portrait pins the half-width at 800 and
  the half-height grows. One virtual unit is `Typer::scale / 2` px, and
  the touch OSD geometry (`g_touch_controls.pause_cx/cy/radius`) is in
  pixels from the window's top-left — convert with `2 / Typer::scale`.
- The ship's first-life READY prints centre-screen, exactly where the
  prompt card sits; `Overlay::respawn_timer` now returns early while
  `tutorial_->owns_input()`.
- The thrust beacon used to be hidden only on CAMERA's entry, which
  touch skips; `enter_step` now clears `beacon_on_` first, and TURN /
  THRUST place theirs afterwards.
- A new e2e driver must be listed in a `ci_shard.sh` shard or every
  shard fails with `FATAL: driver(s) in no shard`.
- Every e2e driver that builds its own pref dir is a fresh install and
  therefore lands on the start screen; export `NEWTONIA_TUTORIAL=0`
  (lib.sh does; `join_link_scenarios.py` sets its own env).
- A python edit script that asserts mid-way leaves the file untouched;
  when replacing a range between two anchors, check nothing else lives
  between them (`apply_camera_choice` was dropped once that way).

## Process notes for whoever picks this up

- Every change lands via PR; the maintainer merges. Watch the PR until
  green, then unsubscribe and arm no further check-ins; report times in
  Australia/Melbourne.
- Commit messages end with the Co-Authored-By and Claude-Session lines
  the session's system reminder gives; no model identifiers in commits,
  PR text or code comments.
- The `.claude/settings.json` pre-commit hook syntax-checks staged
  files; run the Android parse check from CLAUDE.md if you ever touch
  Android-guarded code (the tutorial touches none).
