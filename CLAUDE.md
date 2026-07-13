# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Newtonia is a top-down 2D space shooter written in C++ using SDL2 and OpenGL. It supports single-player and two-player modes and targets multiple platforms: desktop (macOS, Linux, Windows), mobile (iOS, Android), web (WebAssembly), Xbox/GDK, and Steam.

## Build Commands

### Desktop (macOS / Linux)
```sh
make          # Build native executable: ./newtonia
make clean    # Remove build artifacts
```

Compiler: g++ with `-Wall -O3 -std=c++11`. Sources include root, `weapon/`, and `view/`.

#### Steam build (local achievement/overlay testing)
```sh
make steam       # Build ./newtonia-steam with -DSTEAM_BUILD
make steam-clean
```
Requires the Steamworks SDK unzipped at `./sdk/` (gitignored). Also drops `steam_appid.txt` (app 4536720, override with `STEAM_APPID=`) and the Steam runtime library beside the binary, so `./newtonia-steam` runs from the repo root with the Steam client logged in. Steam objects build as `*.steam.o`, never mixing with the plain build's objects. Never ship `steam_appid.txt` — real depots come from `deploy-steam.yml`.

To re-test earns from scratch: run once with `NEWTONIA_RESET_STEAM_STATS=1` (wipes the account's Steam achievements/stats via `ResetAllStats`, then quit), and delete the local lifetime file `~/Library/Application Support/cc.gfm/newtonia/stats.dat` — otherwise `specials_7` re-unlocks on the first kill from the banked type mask.

#### Linux dependencies
```sh
sudo apt-get install -y libsdl2-dev libsdl2-mixer-dev freeglut3-dev
```

#### macOS dependencies
```sh
brew install sdl2 sdl2_mixer
```
GLUT ships with Xcode Command Line Tools (`xcode-select --install`).

#### Windows (MSYS2/MinGW64)
One-time setup (installs MSYS2 via winget, all packages, netplay deps, and
builds + selftests; idempotent, safe to re-run):
```powershell
./setup_windows_build.ps1                # add -SkipNetplay for a plain build
```
Day-to-day, from the **MSYS2 MINGW64** shell (not "MSYS2 MSYS" — only
MINGW64 has g++/SDL on PATH), or from PowerShell via
`$env:MSYSTEM='MINGW64'; C:\msys64\usr\bin\bash.exe -lc 'cd "$PWD" && make -j8'`:
```sh
make -j8              # plain build
make NETPLAY=1 -j8    # netplay build (needs ./netplay-libs — see below)
```
The Makefile's `_NT` branch mirrors `.github/workflows/windows.yml`: fully
static `newtonia.exe` (no MinGW DLLs needed), GUI subsystem so stdout only
shows when piped from a shell (e.g. the `NEWTONIA_NET_SELFTEST=1` gate).
`./build_netplay_deps.sh` has a matching Windows branch: static
libdatachannel + vendored archives copied into `netplay-libs/lib/*.a`, all
linked in one `--start-group` with msys2's static OpenSSL. `%zu` in
`SDL_Log` formats warns (and misprints) under MinGW — cast to `unsigned`
and use `%u`.

#### Syntax-check without a full build
The pre-commit hook in `.claude/settings.json` runs this automatically on staged files:
```sh
g++ -std=c++11 -fsyntax-only -I. -I/usr/include/SDL2 <file.cpp>
```

#### Headless runtime testing & debugging (Linux, no display)
**See TESTING.md for the full test inventory** — build gates, in-binary
selftests (`NEWTONIA_NET_SELFTEST`), signal-worker tests, the committed
netplay e2e drivers (`test/e2e/`), and the `STEAM_BUILD` stub check. This
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
- `kill $PID; wait $PID` reports 143 (SIGTERM) for a healthy shutdown; 139
  means the game segfaulted on its own.
- `xdotool` prints `XGetInputFocus returned the focused window of 1` warnings
  under Xvfb; they are harmless — filter with `grep -v XGetInputFocus`.
- Give the driver a hard `timeout` and log to a file; a hung X client can
  otherwise keep `xvfb-run` alive forever.
- Late-game testing: `NEWTONIA_BETA=1 NEWTONIA_START_GENERATION=N ./newtonia`
  starts a new game at generation N (world size + hazards included). Flagged
  as a cheat, so achievements stay suppressed for that game.

### macOS App Bundle
```sh
make osx      # Build universal arm64+x86_64 .app bundle
```

Signing entitlements live in `macos/` (`Entitlements.plist`, `EntitlementsDevID.plist`).

### Web (Emscripten)
```sh
make web        # Build WebAssembly output to web/dist/
make web-clean  # Remove web build artifacts
```

Requires Emscripten (`emcc`) and TypeScript compiler (`tsc`) on PATH. The web frontend is TypeScript: `tsc -p web/tsconfig.json` compiles `web/main.ts`; `web/shell.html` is the Emscripten shell file. The build links `-lidbfs.js` for IndexedDB persistence and preloads audio assets (`--preload-file audio@audio`). `web_main.cpp` mounts IDBFS asynchronously and only starts the game loop after `web_on_idb_ready()` fires from JS.

Output layout (see `web/README.md`): the marketing landing page (`web/site/`, including the GitHub Pages `CNAME`) is copied to the `web/dist/` root; the playable game builds into `web/dist/play/`. The shell's "back to site" link points at https://newtonia.metonymous.com and opens in a new tab when embedded off-domain (e.g. the itch.io iframe). The build sets `-s GROWABLE_ARRAYBUFFERS=0` explicitly — the emscripten 6.0.2 default-on setting breaks Firefox (its TextDecoder rejects views over resizable ArrayBuffers).

### Android
```sh
make android          # Copy audio assets + build debug APK (app-debug.apk)
make android-install  # Build + install onto a connected device / emulator (adb)
# or, directly:
cd android && ./gradlew assembleDebug
```

`make android`/`make android-install` are thin wrappers over Gradle that first copy `audio/` into `android/app/src/main/assets/` (matching the CI "Copy audio assets" step) so the APK ships its sounds; `make android-clean` runs `gradlew clean` and removes the copied assets. SDL2/SDL2_mixer must still be cloned as siblings to the repo root first (see `.github/workflows/android.yml`). The native build uses the root `CMakeLists.txt` (a generic CMake build that globs all sources, excludes the desktop `glut.cpp` entry point, and clones SDL2/SDL2_mixer from GitHub). Requires Android NDK 26.3.11579264 and CMake 3.22.1 (set in `android/app/build.gradle`; compileSdk/targetSdk 35, minSdk 21; ABIs arm64-v8a, armeabi-v7a, x86_64). The CMake target is `libnewtonia.so` (shared library).

### iOS
Open `ios/Newtonia-iOS.xcodeproj` in Xcode. For simulator builds see `ios/README.md`.

### Xbox / GDK (Windows)
```sh
cmake -B xbox/build-desktop -S xbox -A Gaming.Desktop.x64
```

`xbox/CMakeLists.txt` builds for GDK Desktop (Gaming.Desktop.x64) and Xbox Series (Gaming.Xbox.Scarlett.x64) using the VS2022 MSBuild platform registration installed by GDK 2510+ — no separate toolchain file. Renders via OpenGL ES 2 through ANGLE (libEGL/libGLESv2 from the ANGLE.WindowsStore NuGet package — not bundled with the GDK; located via `ANGLE_INCLUDE_DIR`/`ANGLE_LIB_DIR` or `GDK_ROOT`). See `xbox/PORT_PLAN.md` for the full port plan. Uses static MSVC runtime (`/MT`); `xbox/sdl_gdk_stubs.cpp` provides GDK PLM stub symbols; packaging config in `xbox/MicrosoftGame.config` and `xbox/PackagingLayout.xml`.

### Sound assets
`generate_sounds.py` procedurally generates the WAV files in `audio/`.

### Achievement icons
`generate_achievement_icons.py` (requires Pillow) procedurally generates the 256×256 achieved/locked Steam achievement icon pairs into `steam/icons/` and the 512×512 Game Center variants (achieved art only — Game Center renders its own locked state) into `gamecenter/icons/`, porting the in-game glyph constructions (ship/station/asteroid meshes, Typer font, in-game colours) so the icons match the game's look. One deterministic scene function per §5 symbolic ID — rerun after any achievement list change.

## Architecture

### Class Hierarchy

The codebase separates **game logic** from **rendering** using a GL-prefixed wrapper pattern:

- `Ship` / `Asteroid` / `Pickup` / `Enemy` — pure game logic (physics, health, state)
- `GLShip` / `GLEnemy` / `GLCar` / `GLStarfield` — rendering + input layer wrapping logic classes
- `GLStation` / `GLMiniStation` — exceptions to the pattern: inherit `Ship` directly, combining logic and rendering in one class
- `GLGame` — top-level in-game state, owns all GL* objects, drives the update/draw loop

### Platform Entry Points

| Platform | Entry point |
|----------|-------------|
| Desktop  | `glut.cpp` (GLUT main loop) |
| Android  | `android_main.cpp` (SDL2 event loop, multi-touch) |
| iOS      | `ios/ios_main.mm` |
| Web      | `web_main.cpp` (Emscripten loop, IDBFS persistence) |
| Xbox/GDK | `xbox_main.cpp` (SDL2 event loop, ANGLE GLES2, GDK PLM lifecycle) |
| macOS helper | `macos_window.mm` (window activation for Steam launch) |

### Object Inheritance

All game entities inherit from a common base:

```
Object                          — position, velocity, radius, collision, step()
└── CompositeObject             — owns child Objects (e.g. asteroid fragments)
    ├── Ship                    — player/enemy logic (weapons, health, behaviours)
    │   ├── Enemy               — AI-controlled ship (targeting, difficulty level)
    │   ├── GLStation           — enemy station; deploys waves of GLEnemy ships
    │   └── GLMiniStation       — small drifting station; fires at nearest player
    └── Asteroid                — breakup mechanics, spawns children on death
Pickup (: Object)               — collectible items dropped by asteroids
BlackHole (: Object)            — stationary gravitational hazard
Particle (: Object)             — bullet/trail particle with TTL
```

`Object` provides `step(delta)`, `collide()`, `kill()`, and `is_removable()`. Subclasses override these as needed.

### Game Loop

- `StateManager` owns the current `State` and dispatches input (keyboard, controller, touch, mouse)
- Each frame: `state->tick()` (physics/logic), `state->draw()` (rendering)
- Fixed timestep: `step_size = 8ms`; delta accumulates and runs discrete steps
- World starts at 2500×2500 units (set via `WrappedPoint::set_boundaries`) and grows each generation

### Level / Generation Progression

When all killable asteroids are destroyed, a 5-second countdown (tick sounds) runs, then `generation` increments and the level rebuilds (`glgame.cpp`):

- World grows by 50×50 per generation; at generation 14 it instead grows by 3000×3000
- Asteroid count: `default_num_asteroids + generation * extra_num_asteroids`
- Special asteroid types unlock by generation: reflective ≥ 2, teleporting ≥ 3, invisible ≥ 4, quantum ≥ 5, tough ≥ 6, armoured ≥ 7, phasing ≥ 8 (counts scale with generation)
- `GLMiniStation` (mini-station) spawns from generation ≥ 10
- Black hole spawns at world centre from generation ≥ 13
- `GLStation` (enemy station) spawns from generation ≥ 14
- Pickups are cleared, the starfield and grid are rebuilt, players respawn, and progress is auto-saved
- Every level that introduces a new object type gets an intro screen — the `Intro` state (`intro.h/cpp`; asteroid specials at 1–8, mini-station at 10, black hole at 13, station at 14; a new game starts straight into play): the world freezes and the object spins centre-screen with its name and a flashing "PRESS FIRE TO START" ("TAP FIRE TO START" in touch mode); any player's shoot input (key, controller A/right trigger, the touch fire button — not a tap anywhere) dismisses it, and the touch OSD is drawn on the intro so the fire button is findable, or it auto-starts after 5 s (`Intro::auto_start_ms`) with no input (`GLGame::maybe_start_intro()` decides whether one is due and hands the game to a new `Intro`; not persisted in saves — resuming a save never re-shows one); a looping title-style tune (`audio/intro.wav`) plays on its own channel while other sound channels stay paused, halted on dismissal

## Key Systems

### State Machine

`StateManager` (`state_manager.h/cpp`) drives top-level transitions. Each state inherits from `State` (`state.h/cpp`):

- Pure virtual: `draw()`, `keyboard()`, `keyboard_up()`, `controller()`, `tick()`
- Virtual: `touch_tap()`, `back_pressed()`, `resize()`
- States call `request_state_change()` to transition. `request_state_change(next, true)` transfers ownership of the outgoing state to the next one — the `StateManager` skips its usual `delete` — and `clear_state_change()` resets a stale transition when such a state is later reinstalled

There are three states:
- **Menu** (`menu.h/cpp`) — main menu and options screen, animated starfield, touch support. On non-touch platforms it opens on an attract screen (flashing "PRESS ENTER/START") dismissed by Enter/Space/controller Start. Esc or controller Back opens a quit confirmation (compiled out on web with `__EMSCRIPTEN__`; Android/Xbox reach it via `back_pressed()`). Selecting NEW GAME while a save exists shows a "New game?" YES/NO confirmation (NO is the default; keyboard/controller stack YES above NO, touch puts YES on the left half and NO on the right). Options screen (a data-driven row list, `opt_row`): P1/P2 sensitivity (SLOW–MAX, 0.5–2.0, 5 steps), P1/P2 camera smoothing (OFF–MAX, 0.0–0.010, 5 steps), P1/P2 camera (FIXED/ROTATE, 2 steps), star density (MINIMAL–FULL, 0.1–1.0, 5 steps). Desktop/controller: three lines per row (heading / numbered steps / value), left/right adjusts, Esc/back closes. Touch: P2 rows excluded (mobile shows P1 + shared options), one row each with the name left and value right, tap to cycle, a RETURN TO MENU band exits
- **GLGame** (`glgame.h/cpp`) — in-game; owns all game objects; handles asteroid spawning, pickup drops, two-player split-screen, pause, auto-save. Game over transitions back to Menu (no separate game-over state). **Netplay spectator flow**: online, when the local player runs out of lives while the peer plays on (the game only ends when *all* players are out), a 5 s `SPECTATING IN N` countdown runs on the local wreck (`update_spectate()` / `spectate_arming()`), then the camera hands off to the peer (`camera_target()` returns `remote_player()`) and `SPECTATING` shows at the bottom — the viewer keeps their own rotate/fixed camera preference, the peer's is not adopted. The control OSD and the wreck's own GAME OVER indicator are suppressed while spectating; the shared GAME OVER card takes over once the peer is also out. On the client, losing the host while already out is terminal (goes straight to GAME OVER, no rejoin)
- **Intro** (`intro.h/cpp`) — between-level new-object intro screen. `GLGame::maybe_start_intro()` hands the game to a new `Intro` via ownership transfer; the intro (a `friend` of `GLGame`) draws the frozen world's starfield with the new object spinning centre-screen, and on fire/auto-start hands the game back (`request_state_change(game)`), or deletes it (auto-saving) when leaving to the Menu. `StateManager` forwards focus and controller hot-plug events to it alongside the `GLGame` case

### Weapon System

`Weapon::Base` (`weapon/base.h`) defines the interface: `shoot()`, `step()`, ammo tracking. Each weapon has its own `.h`/`.cpp` in `weapon/`:

| File | Weapon | Notes |
|------|--------|-------|
| `weapon/default` | Default gun | Automatic/semi-auto; `level` controls accuracy; `time_between_shots` |
| `weapon/mine` | Mine | Deployable, limited ammo, large blast |
| `weapon/giga_mine` | Giga Mine | Larger blast than mine |
| `weapon/missile` | Missile | Homing AI; `set_asteroids()` / `set_ship_targets()`; seeks via `query_segment()` |
| `weapon/shield` | Shield | Energy barrier, limited ammo |
| `weapon/god_mode` | God Mode | Timed invincibility (10s); fires periodic shockwaves (150ms); plays special music with a warning phase in the final 3s |
| `weapon/nova` | Nova | Secondary weapon; charges accumulate from asteroid kills (0–9); triggers `ship->nova_detonate()` |
| `weapon/beam` | Pierce Beam | Primary weapon; limited ammo; fires a single fast bolt (`piercing` flag on the `Particle`) that ploughs straight through a line of asteroids instead of stopping at the first, but only continues through asteroids it actually destroys — it stops when it hits one it can't destroy (invincible, or tough not yet broken); one bolt per trigger pull |
| `weapon/lance` | Lance | Primary weapon; limited ammo; one instantaneous full-length pulse per trigger pull (`Lance::RANGE` = the beam bolt's total travel). The weapon just sets `ship->lance_pulse_pending`; `Ship::fire_lance_pulse()` ray-marches it with the grid: kills every killable asteroid along the line — including tough ones, which the lance kills outright — mirror-reflects (carrying remaining distance) off surfaces that reflect bullets (reflective asteroids, armoured faces, phased ghosts), and is blocked by plain invincible asteroids and teleport evades. The traced polyline is kept as a fading `LancePulse` drawn by `GLShip`. Ship/station hits resolve in `GLGame::resolve_lance_ship_hits` (the march only sees asteroids): the firer dies to post-reflection segments only, the partner needs friendly fire on, enemies and the mini-station die (scored like bullet kills), the gen-20 station hull takes multi-hit damage; online a client's polyline is resolved host-side from MSG_LANCE, except enemies, which the client kills instantly and claims with bullet_id 0 (PROTO 20) like bullet claims |

### Pickup System

All inherit from `Pickup` base class (`pickup.h`). Each pickup implements `draw()` and `apply(Ship*)`. Root-level pairs to each weapon:

| File | Pickup | Effect |
|------|--------|--------|
| `weapon_pickup` | Weapon | Generic gun drop; `weapon_index` selects type |
| `mine_pickup` | Mine | +N mine shots |
| `giga_mine_pickup` | Giga Mine | +N giga mine shots |
| `missile_pickup` | Missile | +N homing missiles |
| `shield_pickup` | Shield | +N shield charges |
| `god_mode_pickup` | God Mode | +10s invincibility |
| `nova_charge_pickup` | Nova Charge | +1 nova charge (auto-drops every 100 asteroid kills) |
| `beam_pickup` | Pierce Beam | +20 beam bolts (violet star) |
| `lance_pickup` | Lance | +10 lance pulses (amber star) |
| `revive_pickup` | Revive | Co-op only (green cross): revives the fallen partner on their last life; GLGame applies it at the collection site (the pickup can't see the player list) |
| `extra_life` | Extra Life | +1 life (heart shape) |

**Drop chances** (per asteroid death, constants in `glgame.cpp`): extra_life 0.3125%, weapon 1.25%, mine 1.25%, giga_mine 0.5%, missile 1.25%, shield 1.25%, god_mode 0.25%, beam 0.75%, lance 0.5%. **Revive** is a separate 10% roll ahead of that table, active only while some player is fully out of lives with a partner still in it, and capped at one in the world at a time; collecting it sets the fallen partner's `lives = 1` and restarts their respawn countdown (`GLGame::revive_fallen_partner`), which online replicates like any respawn and ends the spectator flow by itself.

### Asteroid Special Types

Asteroids (`asteroid.h/cpp`) are 9-vertex irregular polygons with per-vertex radius offsets. Special flags:

| Flag | Behaviour |
|------|-----------|
| `invincible` | Cannot be destroyed by bullets |
| `invisible` | Only visible as gravitational-lens distortion (rendered via `WarpPass`) |
| `reflective` | Bounces bullets back |
| `teleporting` | Randomly teleports; has a brief vulnerability window (`teleport_vulnerable`) |
| `quantum` | Collapses to solid when observed by a player |
| `tough` | Requires 5 hits; shows crack lines during damage (`crack_vertex`, `crack_t`, `crack_perp`) |
| `armoured` | One rotating face deflects bullets; `armor_angle` tracks the weak spot |
| `elastic` | Bounces off other elastic asteroids; not set independently — derived from `reflective` in the constructor |
| `phasing` | Cycles between solid and intangible states (`phased`, `phase_timer`) |

Serialization: `capture_state()` / `restore_state()` for save/load.

### Behaviours

`Behaviour` is an abstract base (`step(delta) = 0`) holding a pointer to a `Ship` and a `done` flag. Ships maintain a list of active behaviours run each frame. Each gets its own `.h`/`.cpp`:

| File | Behaviour | Notes |
|------|-----------|-------|
| `follower` | Follower | Enemy AI; locks onto targets; burst shooting; avoidance logic; `difficulty` param |
| `shield_behaviour` | ShieldBehaviour | Timed shield effect |
| `teleport` | Teleport | Teleportation event for enemies |

### Collision Detection

**Grid** (`grid.h/cpp`) provides spatial partitioning. Objects register themselves each frame; `collide()` finds neighbors; `query_segment()` supports line queries (missile homing). Fixed-radius proximity checks via `Object::collide()`. `grid_debug.cpp` implements `Grid::draw_debug()`, a collision-grid overlay toggled in-game (default key `b`).

### Coordinate System

- **Point** (`point.h/cpp`) — 2D vector (x, y)
- **WrappedPoint** (`wrapped_point.h/cpp`) — toroidal wrapping; objects exiting one edge reappear on the opposite side

### Rendering

**OpenGL compatibility** — `gl_compat.h` and `gles2_compat.h/cpp` abstract desktop OpenGL 3.3 Core vs OpenGL ES 2 (mobile/web/Xbox). `gles2_compat` provides GLSL program wrappers, vertex/color buffers, and line-thickening for WebGL (which disallows `lineWidth > 1`).

**Mesh system** (`mesh.h/cpp`) — GPU geometry with interleaved position/colour data. Builder API:
```cpp
MeshBuilder::begin(mode) → color() → vertex() → end() → Mesh
mesh.upload(); mesh.draw(); mesh.draw_tinted(); mesh.draw_at(); mesh.draw_with_model();
```

**Text** — `Typer` (`typer.h/cpp`): static `draw()`, `draw_centered()`, `draw_lefted()`, `draw_lives()`. Per-character GPU meshes; dynamic window resize.

**WarpPass** (`warp_pass.h/cpp`) — gravitational-lens distortion shader for invisible asteroids. Captures viewport to texture, applies radial-distortion pass. Works on both GLSL 1.50 (desktop) and GLSL ES 1.00 (mobile/web).

**Particles** — `Particle` (`particle.h/cpp`): `Object` subclass with TTL countdown; `GLTrail` (`gltrail.h/cpp`) renders thruster particle trails. A particle can be flagged `kills_invincible` to penetrate shields.

**Split-screen** — `GLGame` renders into multiple viewports for two-player mode. `view/overlay.cpp` handles all HUD elements: score, lives, weapons, temperature, level, respawn timer, keymap, god-mode indicator, touch controls, edge indicators, debug info.

**Starfield** — `GLStarfield` (`glstarfield.h/cpp`): parallax background with layered star densities; star count scales with the `star_density` preference (`star_density_scale()`).

**AsteroidDrawer** (`asteroid_drawer.h/cpp`) — renders asteroids; animates crack lines on tough asteroids.

**BlackHole** (`black_hole.h/cpp`) — stationary hazard; `apply_gravity(Object, delta, gravity_scale)` returns `true` if an object crossed the event horizon; minimap ring rendering.

### Ship Details

`Ship` (`ship.h/cpp`) key features:
- **Heat system**: `temperature`, `max_temperature`, `critical_temperature`, `explode_temperature`, `temperature_ratio()`; thrust/reverse build heat, cooling over time
- **Nova system**: `nova_charge` (0–9), `nova_kill_counter` (0–99), `nova_drops_pending`
- **Invincibility**: `time_left_invincible`
- **Respawn**: `respawn_time`, `time_until_respawn`
- **Weapon management**: `next_weapon()`, `previous_weapon()`, `add_weapon(index)`, `add_mine_ammo()`, `add_giga_mine_ammo()`, `add_missile_ammo()`, `add_shield_ammo()`, `add_god_mode()`, `add_nova_charge(n)`
- **Behaviours**: `add_behaviour()`, `disable_behaviours()`

`Enemy` (`enemy.h/cpp`) — inherits `Ship`; parameterized by `difficulty`; takes target and asteroid lists for the `Follower` behaviour.

`GLShip` (`glship.h/cpp`) — rendering + input wrapper: keyboard, controller (SDL_GameController with analog sticks), touch joystick. Camera following with optional rotation (`toggle_rotate_view`) and configurable smoothing rate (`camera_smoothing` preference; 0 = instant snap).

`GLEnemy` (`glenemy.h/cpp`) — rendering wrapper for `Enemy`.

`GLCar` (`glcar.h/cpp`) — alternative player ship model with left/right jets.

`GLStation` (`glstation.h/cpp`) — inherits `Ship`; rotating ring station with `health` and per-wave difficulty that deploys waves of `GLEnemy` ships; `capture_state()` / `restore_state()` for save/load.

`GLMiniStation` (`gl_mini_station.h/cpp`) — inherits `Ship`; small station that drifts in one random direction, passes through asteroids, and periodically fires at the nearest player (its bullets destroy asteroids but award nothing). Dies to a single player shot for a fixed reward (`REWARD = 1000`, awarded in `GLGame`); `capture_state()` / `restore_state()` for save/load.

### Save / Load

**Savegame** (`savegame.h/cpp`) — binary format, magic "NWTN", version 14:
- `WeaponEntry`: kind, weapon_index, ammo (kinds include the primaries `Beam` and `Lance`)
- `Player`: score, lives, kills, respawning flag, position, velocity, facing, weapons, nova state, achievements bookkeeping (asteroid kills, enemy-ship kills, died-this-generation, weapons-fired mask; appended in v14 together with the game-scoped cheat flag)
- `Asteroid`: position, velocity, radius, health, all special flags and transient state
- `Pickup`: type, position, weapon_index
- `BlackHole`, `Enemy`, `Station`: positional/state data (Station includes its deployed enemies)
- `MiniStation`: present flag, alive, position, drift velocity, rotations, shot timer
- `GameState`: generation, world size, level_cleared, players, all object lists, game-scoped achievements cheat flag (appended in v11)

**Backward compatibility:** `GameState::MIN_VERSION..VERSION` all load; older or newer files are ignored. New fields are only ever **appended at the end** and read back gated on `version >= N`, so an older save stops short and the new fields take their defaults (e.g. v9 saves load with no mini-station). Loading then re-saving upgrades the file to the current `VERSION`. Keep this convention when bumping the version so existing saves survive.

**Netplay reuses these structs — update the snapshot rebuild too.** Snapshots serialize through the same `Save::` types, and the restore logic exists in TWO places: the savefile-load switch in the `GLGame(save)` constructor AND the wholesale rebuild in `net_apply_state()` (what a net client applies 10x/s). Anything added to the savefile — a new `PickupType`, `WeaponEntry::Kind`, object list — must be handled in **both**, or the addition silently vanishes on net clients (a missing case is skipped, not an error: the Beam/Lance pickups arrived in client snapshots invisible for exactly this reason). Grep for the existing enum's cases and extend every switch you find.

Auto-save triggers on pause or player death if the player has lives or score remaining, and on level completion.

**Preferences** (`preferences.h/cpp`) — INI file in SDL pref path; global `g_prefs` instance:
- Per-player (`PlayerKeys`): 12 key bindings (left, right, thrust, shoot, reverse, mine, next_weapon, next_secondary, boost, teleport, help, toggle_rotate_view; defaults WASD + Space/X) plus `keyboard_sensitivity` and `camera_smoothing` floats and a `rotate_view` bool (per-player camera fixed/rotate; the in-game toggle key and the Options screen both write it)
- General (`GeneralKeys`): pause (P), menu (Esc), add_player2 (Enter), toggle_friendly_fire (G), skip_level (N), toggle_debug_grid (B), time_speed_up (=), time_slow_down (-), time_reset (0), toggle_fullscreen (F)
- Display: `fullscreen` flag, `window_width`/`window_height`
- Camera: legacy global `rotate_view` flag — superseded by the per-player `PlayerKeys::rotate_view`; kept only as a load-time migration seed (old INIs) and a downgrade fallback written from P1
- Gameplay: `friendly_fire` flag, `star_density` multiplier (user-editable float in the INI)
- API: `load_preferences()`, `save_preferences()`; missing keys in old files are silently ignored

**High Score** (`highscore.h`) — `load_high_score()` / `save_high_score(score)`.

### Achievements & Lifetime Stats

Full design in `ACHIEVEMENTS.md` (platform requirements, master list, backend plan).

**Achievements** (`achievements.h/cpp`) — platform-neutral seam: `Achievements::unlock(id)` / `progress(id, pct)` (percent, 100 == unlock) with symbolic string IDs; the default backend is a no-op; platform backends replace it behind their own build flags (Steam: `steam_achievements.cpp` under `STEAM_BUILD`; Game Center: `game_center_achievements.mm` under `GAME_CENTER_BUILD`, defined by the iOS Xcode project and the ios.yml simulator CI, both linking GameKit; `Achievements::init()` is called from `ios_main.mm`; GDK in the private mirror; Play Games: `play_games_achievements.cpp` under `PLAY_GAMES_BUILD`, defined by the root `CMakeLists.txt` for every Android build — a JNI bridge to `android/app/src/main/java/org/newtonia/PlayGamesAchievements.java`, which talks to Play Games Services v2 (automatic sign-in; `unlock` + 100-step incremental `setSteps`; console-generated IDs pasted into `android/app/src/main/res/values/games-ids.xml` under the resource names fixed by the native mapping table — entries stay commented out until the Play Console project exists, and missing entries just drop earns with a logcat warning); `Achievements::init()` is called from `android_main.cpp` after `SDL_Init`, `NewtoniaActivity.onResume()` retries sign-in and flushes the in-memory pre-sign-in earn queue, and offline delivery is Play services' job — this backend never touches the pending journal). Backends whose SDK doesn't queue offline earns (Game Center now, GDK next) record every earn in the shared pending journal (`achievement_journal.h/cpp` → `pending_achievements.dat` in the pref path, keyed by symbolic ID + platform account so profile switches can't cross-credit; single-threaded — marshal SDK completions to the game thread) before attempting delivery, confirm entries out only once the server's achievement list shows them, and resubmit the rest on sign-in/foreground. Local Xcode device builds sign with `ios/EntitlementsDev.plist` (project `CODE_SIGN_ENTITLEMENTS`) so sandbox Game Center works; releases use `ios/Entitlements.plist` injected by deploy-ios.yml. `coop_clear` is deliberately unmapped in the Game Center table until netplay makes 2P earnable on touch devices (ACHIEVEMENTS.md §5); Game Center dev reset: `NEWTONIA_RESET_GAME_CENTER=1` via the Xcode scheme. The shared layer owns the XR-057 cheat-suppression flag, which is **game-scoped**: skip-level and time-scale keys call `note_cheat_used()`, `unlock`/`progress` are dropped for the rest of that game, and only a fresh game (`new_game_started()`) clears it — deliberately not per-generation, or skipping to one level short of a progression achievement and clearing a single level would unlock it. The flag rides the savegame (`GameState::cheated`, v11) so save/quit/resume doesn't launder it.

Hooks: `GLGame` (level clear, generation rebuild/progression, station + mini-station destruction, cheat keys) and `Ship` (`credit_asteroid_kill()` shared by every asteroid-kill path, `credit_ship_kill()` on the bullet/missile ship-kill paths, weapon-kind tracking in `shoot()`/`fire_secondary()`/`add_god_mode()`, `nova_detonate()`, death flag in `kill()`). Attribution: only ships with `is_local_player` (set by `GLGame` when creating player ships; false for enemies, stations, and future remote netplay peers) earn achievements and stats.

**Presence** (`presence.h/cpp`) — platform-neutral online-status seam mirroring the achievements pattern: `Presence::set_menu()` / `set_level(level, num_players)` / `clear()`; the shared layer dedupes repeat reports (logging each change to stdout — greppable in headless tests) and the default backend is a no-op. The Steam backend (`steam_presence.cpp` under `STEAM_BUILD`) sets Rich Presence via `ISteamFriends::SetRichPresence`: a plain-English `status` key plus a `steam_display` localization token (`#StatusMenu`, `#StatusLevel`, `#StatusLevelCoop`; `%level%` substituted from the `level` key) so friends see "Level 3" / "Level 3 Co-Op". The tokens live in `steam/rich_presence.vdf`, pasted manually into the Steamworks portal (App Admin → Community → Rich Presence) after any change — no workflow uploads it. Hooks: `Menu` constructor (menu status); `GLGame::update_presence()` from both `GLGame` constructors, the generation increment, and all player-2 join paths (both local joins and the netplay `add_remote_player()`); `glut.cpp` clears presence at exit. Levels are reported as displayed numbers (generation + 1). No cheat suppression — presence is descriptive, not an earn.

**Invites** (`invites.h/cpp`) — platform-neutral game-invite seam mirroring the presence/achievements pattern: `Invites::init()` / `set_joinable(room_code)` / `clear_joinable()` / `poll_accepted_invite(code_out)` / `capture_launch(argc, argv)`. **The room code is the universal join token** — the platform invite system only ferries it host→joiner; the actual connection still runs over signaling + WebRTC, so no platform networking is involved. The shared layer owns the connect-string parse (`"+connect <code>"`, bare code tolerated) and the pending-code handoff; the default backend is a no-op. The Steam backend (`steam_invites.cpp` under `STEAM_BUILD`) advertises via `ISteamFriends::SetRichPresence("connect", "+connect <code>")` (a non-empty `connect` key gives friends the "Join Game" option for free) and catches accepts with a `GameRichPresenceJoinRequested_t` callback (running game) or the `+connect` launch arg (cold launch, `capture_launch`). Hooks: `glut.cpp` calls `Invites::init()` + `capture_launch()` at startup; `NetLobby` calls `set_joinable()` when its room code arrives and `clear_joinable()` in its destructor (slot full or host left); `Menu::tick()` polls `poll_accepted_invite()` and routes the code to `new NetLobby(code)` (the existing programmatic-join ctor). Xbox/GDK (MPSD + activity handle) slots in behind its own flag later.

**Lifetime stats** (`stats.h/cpp`) — standalone roaming `stats.dat` in the SDL pref path (magic "NWST", version 1, append-only format like the savegame): lifetime asteroid kills and a special-type kill mask, deliberately outside `savegame.dat` so netplay still counts. Kill writes are batched (every 10) and flushed via `Stats::flush()` from `save_progress()` and game over. Writes are skipped while the cheat flag is set — `specials_7`/`kills_10000_lifetime` read this file, so cheat games banking lifetime progress would launder achievements. Likely needs adding to Steam Auto-Cloud patterns so it persists across installs.

### Touch Controls

`touch_controls.h/cpp` — maps screen zones to ship actions for iOS/Android:
- Left half: virtual joystick (`joy_nx`, `joy_ny`, `joy_cx`, `joy_cy`, `joy_radius`)
- Right half: shoot / mine buttons
- `resize()` handler for dynamic layout

### Audio

SDL_mixer; all assets are WAV files in `audio/` (generated by `generate_sounds.py`). Mobile builds copy them into the app bundle/assets. God Mode weapon plays special music with a warning phase in the final 3 seconds. Music tunes (all title-style A-minor synth pieces): menu `title.wav` (8s, `Mix_PlayMusic`), intro screens `intro.wav` (4s loop on its own channel), pause screen `pause.wav` (16s loop on its own channel, silenced while the window is unfocused).

## CI/CD

GitHub Actions runs builds on every push to `master`/`main` and on PRs (feature branches build once via their PR, not twice):

| Workflow | Output |
|----------|--------|
| `.github/workflows/macos-dev.yml` | Universal arm64+x86_64 binary |
| `.github/workflows/android.yml` | Debug APK |
| `.github/workflows/ios.yml` | iOS simulator build |
| `.github/workflows/linux.yml` | Linux executable (netplay + headless loopback self-test) |
| `.github/workflows/windows.yml` | Windows executable (netplay: MinGW-static libdatachannel + self-test — the compile gate for deploy-steam's Windows build) |
| `.github/workflows/web.yml` | WebAssembly + GitHub Pages deploy (master/main only) |
| `.github/workflows/xbox-dev.yml` | GDK Desktop (Gaming.Desktop.x64) build — catches Xbox-port compile errors without hardware |
| `.github/workflows/xbox-console-smoke.yml` | Compile-only check of the `_GAMING_XBOX` console paths with MSVC under `WINAPI_FAMILY_GAMES` (no GDKX/NDA material; GDK-only headers stubbed in `xbox/smoke_stubs/`) |

**Deployment workflows** (triggered by version tags or manual dispatch). Two tag namespaces: `v*.*.*` is the normal release pipeline; `netplay-v*` is a netplay test release routed to test channels (Steam `netplay` branch, itch `html5-netplay` channel; TestFlight/Play-internal are test tracks either way). The globs can't overlap, so netplay tags never touch the normal pipeline:
- `.github/workflows/deploy-steam.yml` — Steam (Windows/macOS/Linux via Steamworks SDK); manual dispatch defaults to the `netplay` test branch, `v*.*.*` tags go to `beta`, `netplay-v*` tags to `netplay`
- `.github/workflows/deploy-ios.yml` — TestFlight
- `.github/workflows/deploy-android.yml` — Play Store
- `.github/workflows/deploy-itch.yml` — Itch.io (pushes only the playable game `web/dist/play`, not the landing page)

All deploy artifacts build with netplay (NETPLAY.md M3-5): web/Android have it inherently (Emscripten backend is unconditional; root CMakeLists defaults `NEWTONIA_NET=ON`), deploy-ios feeds the device libdatachannel build through the pbxproj's `NEWTONIA_NET_DEFINE`/`NEWTONIA_NET_HEADER_PATH` vars, and deploy-steam builds libdatachannel per platform. Each native deploy job runs the headless `NEWTONIA_NET_SELFTEST` loopback as a gate; the dev workflows above prove the same recipes on every push.

**Disabled workflows** — `.github/workflows/disabled/` holds inactive deployment workflows (`deploy-macos.yml`, `deploy-windows.yml`, `deploy-xbox.yml`); move a file back into `workflows/` to re-enable it.

**Steam integration** — `steam_build.h` (constants/SDK), `steam_achievements.cpp` (achievements backend behind `STEAM_BUILD`; symbolic→`ACH_*` mapping, progress via increment-only pct stats), `steam_presence.cpp` (rich-presence backend behind `STEAM_BUILD`), `steam/` contains Steamworks VDF config files (`app_build.vdf`, `depot_build_windows.vdf`, `depot_build_macos.vdf`, `depot_build_linux.vdf`, plus `rich_presence.vdf` — pasted manually into the portal, not uploaded by a workflow).

## Conventions & Patterns

1. **GL-prefix pattern** — Rendering wrappers (`GLShip`, `GLGame`, `GLEnemy`, `GLCar`) wrap pure logic classes. Never put rendering into logic classes. (`GLStation` and `GLMiniStation` are the existing exceptions — they inherit `Ship` directly.)
2. **Weapon/pickup pairs** — Every weapon type has a corresponding `*_pickup` class at root level.
3. **Behaviour pattern** — Abstract `Behaviour` base with `done` flag; `Ship` owns a list and runs them each frame.
4. **State machine** — `StateManager` + `State` subclasses drive all top-level transitions.
5. **Serialization** — Major game objects implement `capture_state()` / `restore_state()` pairs.
6. **Grid collision** — Use `Grid` for all spatial queries; never iterate all objects.
7. **Mesh builder** — All geometry is pre-uploaded to GPU VBOs via `MeshBuilder`; no immediate-mode GL calls.
8. **Platform abstraction** — Use `gl_compat.h` macros; never call desktop-only GL functions directly.
9. **File naming** — Behaviours: `*_behaviour.h/cpp`. Weapons: `weapon/*.h/cpp`. Pickups: `*_pickup.h/cpp` at root. Views/HUD: `view/*.h/cpp`.
10. **C++11** — Codebase targets C++11 (`-std=c++11`). Avoid later standard features.
11. **New .cpp files must be added to `ios/Newtonia-iOS.xcodeproj/project.pbxproj`** — every other build discovers sources automatically (Makefile/web/iOS-CI/macOS-CI glob or `find`; Android/Xbox CMake `GLOB_RECURSE`), but the Xcode project lists files explicitly, and iOS CI compiles via `find` rather than the project, so a missing entry breaks only local Xcode builds and goes unnoticed by CI. Add each new `.cpp` (headers too, for navigation) to all four sections: PBXBuildFile, PBXFileReference, the Sources group children, and the PBXSourcesBuildPhase, following the existing `AA…`/`AB…` ID scheme.
