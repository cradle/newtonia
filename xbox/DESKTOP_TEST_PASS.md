# GDK Desktop — manual test pass checklist (Phase 1)

Validates the Windows GDK Desktop build at runtime. Needs: a Windows 10/11
PC, one or two Xbox controllers (USB or Bluetooth), ~15 minutes.
Record results inline (✅/❌ + notes) and file anything broken as an issue
referencing `xbox/PORT_PLAN.md` Phase 1.

## Setup

1. Download the `newtonia-gdk-desktop` artifact from the latest green
   **Xbox GDK Desktop** run (GitHub → Actions → Xbox GDK Desktop), unzip.
2. Confirm contents: `newtonia.exe`, `libEGL.dll`, `libGLESv2.dll`,
   `MSVCP140_APP.dll`, `VCRUNTIME140_APP.dll`, `VCRUNTIME140_1_APP.dll`,
   `audio/` (29 WAVs).
3. Useful paths while testing:
   - Log: `newtonia.log` next to the exe (recreated each launch).
   - Prefs/saves: `%APPDATA%\cc.gfm\newtonia\`
     (`preferences.ini`, `savegame.dat`, high score).

## A. Boot & menu

- [ ] Double-click `newtonia.exe`: 1280×720 resizable window appears,
      title music plays, starfield animates. No console window.
- [ ] `newtonia.log` shows the init sequence ending in `Entering main loop`
      with no errors after `SDL_Init OK`.
- [ ] Menu navigation works with keyboard (arrows/enter) AND controller
      (D-pad/A). With a controller connected, attract text says
      "press start" (not "press enter").

## B. Options screen

- [ ] All 5 rows reachable and adjustable (P1/P2 sensitivity, P1/P2
      smoothing, star density) with both keyboard and controller.
- [ ] Change star density to MINIMAL, quit the game entirely, relaunch:
      setting persisted (check `preferences.ini` `star_density`).

## C. Single-player gameplay (keyboard)

- [ ] Start game; WASD + Space controls work; asteroids split; pickups
      drop; HUD shows score/lives/weapon/temperature.
- [ ] F1 toggles the help overlay (shows keyboard names + cheats section).
- [ ] P pauses ("press p to resume" shown); audio pauses too.
- [ ] N (skip level) advances generation — countdown ticks then rebuild.
- [ ] Invisible asteroid lensing (generation ≥ 4, or after several skips):
      warp distortion visible, no GL errors in log.

## D. Single-player gameplay (controller)

- [ ] Unplug/replug or connect controller mid-game: input switches over;
      disconnect while playing auto-pauses.
- [ ] Left stick analog steering/thrust, R2 shoot, L2 mine, X/Y weapon
      cycling, LB boost, RB teleport, R3 toggles help overlay.
- [ ] Help overlay in controller mode shows button glyphs and does NOT
      show FULLSCREEN / FRIENDLY FIRE / CHEATS rows.
- [ ] START pauses; "press start to resume" shown.

## E. Save / resume

- [ ] Play to some score, pause (auto-saves), quit via Esc → menu → quit.
- [ ] Relaunch: menu offers resume; resuming restores score, lives,
      generation, world size, weapons.
- [ ] Delete `savegame.dat` while game closed → relaunch boots clean
      (no crash, no resume offered).

## F. Two players

- [ ] With a second controller connected, START on it (or Enter on
      keyboard) joins player 2: split-screen, center-line divider,
      minimap moves to center.
- [ ] Both players controllable simultaneously; G toggles friendly fire
      (HUD indicator appears).
- [ ] When one player is permanently out (dead, no lives), their viewport
      stays on screen (by design — split-screen does NOT collapse to one);
      the surviving player keeps playing in their half.
- [ ] Disconnect the dead (game-over) player's controller: the surviving
      player keeps playing, game does NOT pause (issue: dead-player
      disconnect must not interrupt the survivor).
- [ ] Disconnect a still-alive player's controller (or one mid-respawn):
      game DOES auto-pause as normal.

## G. Window management

- [ ] Drag-resize the window repeatedly (including very small and very
      large): rendering stays correct (pbuffer recreated), text scales,
      no stretching artifacts, log shows no `eglCreatePbufferSurface`
      failures.
- [ ] F toggles fullscreen; cursor hides in fullscreen; toggle back
      restores prior size/position; setting persists across relaunch.
- [ ] Minimize during gameplay → restore: game auto-paused on minimize,
      music paused, resumes from pause without a timing jump (no
      teleporting objects — `s_reset_tick` working).

## H. Focus & suspend integrity

- [ ] Alt-Tab away mid-game: auto-pause + auto-save (check `savegame.dat`
      mtime). Alt-Tab back: resumes.
- [ ] Kill the process (Task Manager) mid-game after a pause: relaunch
      resumes from the last auto-save without corruption.

## I. Audio

- [ ] All effect classes audible: shoot, explosion, thud/ting (asteroid
      bounces), pickup, boost, shield hum, mine blast, level-clear ticks,
      god-mode music (grab a god-mode pickup; music warns in last 3 s).
- [ ] No crackling/dropouts during heavy combat (48 kHz / 32 channels).

## J. Performance (informational — Desktop blit path is a dev vehicle)

- [ ] At 1280×720: smooth (~60fps-feeling) in normal play.
- [ ] Maximized on a high-res display: note any sluggishness — expected
      from the `glReadPixels`+`StretchDIBits` present path; record how
      bad it is at your resolution. Console uses a real swap chain, so
      this is Desktop-only debt.

## K. Exit

- [ ] Esc from menu → Quit? confirm → process exits cleanly (no hang,
      no crash dialog); log ends without errors.

## Results

| Date | Tester | Build (commit) | Result | Notes |
|------|--------|----------------|--------|-------|
| 2026-06-12 | glenn | `cd63075` | ❌ A/D | Controller hot-plug dead: pad connected after launch detected ("press start") but no input. Pre-connected pad OK. → issue #287 |
| 2026-06-12 | glenn | `130d89a` | ✅ A/D | Hot-plug fix confirmed (open-on-DEVICEADDED, issue #287 closed). Remaining sections not yet run. |
