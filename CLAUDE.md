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

#### Linux dependencies
```sh
sudo apt-get install -y libsdl2-dev libsdl2-mixer-dev freeglut3-dev
```

#### macOS dependencies
```sh
brew install sdl2 sdl2_mixer
```
GLUT ships with Xcode Command Line Tools (`xcode-select --install`).

#### Syntax-check without a full build
The pre-commit hook in `.claude/settings.json` runs this automatically on staged files:
```sh
g++ -std=c++11 -fsyntax-only -I. -I/usr/include/SDL2 <file.cpp>
```

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

### Android
```sh
cd android && ./gradlew assembleDebug
```

The native build uses the root `CMakeLists.txt` (a generic CMake build that globs all sources, excludes the desktop `glut.cpp` entry point, and clones SDL2/SDL2_mixer from GitHub). Requires Android NDK 26.3.11579264 and CMake 3.22.1 (set in `android/app/build.gradle`; compileSdk/targetSdk 35, minSdk 21; ABIs arm64-v8a, armeabi-v7a, x86_64). The CMake target is `libnewtonia.so` (shared library).

### iOS
Open `ios/Newtonia-iOS.xcodeproj` in Xcode. For simulator builds see `ios/README.md`.

### Xbox / GDK (Windows)
```sh
# GDK Desktop — GDKX-free, standalone, no GDK and no -A (what xbox.yml builds):
cmake -B xbox/build-desktop -S xbox        # add -G Ninja to match CI exactly
cmake --build xbox/build-desktop --config Release

# Xbox Series console — GLon12 (decided path): Ninja + NDA GXDK, no -A.
cmake -G Ninja -B xbox/build -S xbox -DXBOX_SCARLETT=ON \
  -DGXDK_INCLUDE_DIR=... -DGXDK_LIB_DIR=... -DGLON12_LIB=...\opengl32.lib
cmake --build xbox/build
# then package: xbox/build_console_package.ps1  (makepkg -> .xvc)

# Xbox Series console — ANGLE fallback (abandoned, GDKX MSBuild platform):
cmake -B xbox/build -S xbox -A Gaming.Xbox.Scarlett.x64
```

`xbox/CMakeLists.txt` builds **three modes**; `xbox_main.cpp` documents them:

- **GDK Desktop** (default — any configure *without* `-A Gaming.Xbox.*` and without `-DXBOX_SCARLETT`, including the plain Ninja CI build): the **desktop GL 3.3 core renderer over SDL's WGL backend** (`SDL_GL_CreateContext`/`SDL_GL_SwapWindow`) — the GLon12-capable path (Option A), `_GAMING_DESKTOP`. Needs neither ANGLE nor the GDK; links SDL2 + `opengl32`. Drop Mesa's GLon12 `opengl32.dll` next to the exe to run the whole game through OpenGL-on-D3D12 (`xbox/GLON12_SPIKE.md`). Active CI path (`xbox.yml`).
- **Xbox Series console — GLon12** (`-DXBOX_SCARLETT=ON`, Ninja + GXDK — **the decided console path**): reuses the *same* desktop-GL renderer + SDL_GL present path as GDK Desktop (`_GAMING_DESKTOP`), adding console-only runtime bits (native-res fullscreen, GDK PLM lifecycle, TV title-safe inset) under `NEWTONIA_GDK_CONSOLE`, compiled against the Game OS API partition (`WINAPI_FAMILY_GAMES`). Links `xgameplatform.lib`/`xgameruntime.lib` + the Xbox GLon12 `opengl32` import lib (`/NODEFAULTLIB`). Packages via `xbox/build_console_package.ps1` / `deploy-xbox.yml` → `.xvc`. GXDK-gated TODOs: a Mesa-for-Xbox GLon12 redist and SDL's `VisualC-GDK` backend.
- **Xbox Series console — ANGLE** (`-A Gaming.Xbox.Scarlett.x64`, `NEWTONIA_XBOX_CONSOLE`/`_GAMING_XBOX`): OpenGL ES 2 through ANGLE (libEGL/libGLESv2 from the ANGLE.WindowsStore NuGet) with a manual EGL context. **Abandoned fallback**, kept compiling for reference only.

Pre-approval hardware testing now goes **package → publish to Partner Center sandbox → install on a retail console via the Xbox app** (2026 onboarding change; see `xbox/PORT_PLAN.md` §0). See `xbox/PORT_PLAN.md` for the full port plan. Uses static MSVC runtime (`/MT`); `xbox/sdl_gdk_stubs.cpp` provides GDK PLM stub symbols; packaging config in `xbox/MicrosoftGame.config` and `xbox/PackagingLayout.xml`.

### Sound assets
`generate_sounds.py` procedurally generates the WAV files in `audio/`.

## Architecture

### Class Hierarchy

The codebase separates **game logic** from **rendering** using a GL-prefixed wrapper pattern:

- `Ship` / `Asteroid` / `Pickup` / `Enemy` — pure game logic (physics, health, state)
- `GLShip` / `GLEnemy` / `GLCar` / `GLStarfield` — rendering + input layer wrapping logic classes
- `GLStation` — exception to the pattern: inherits `Ship` directly, combining logic and rendering in one class
- `GLGame` — top-level in-game state, owns all GL* objects, drives the update/draw loop

### Platform Entry Points

| Platform | Entry point |
|----------|-------------|
| Desktop  | `glut.cpp` (GLUT main loop) |
| Android  | `android_main.cpp` (SDL2 event loop, multi-touch) |
| iOS      | `ios/ios_main.mm` |
| Web      | `web_main.cpp` (Emscripten loop, IDBFS persistence) |
| Xbox/GDK | `xbox_main.cpp` (SDL2 event loop). Two halves: **GDK Desktop** (`_GAMING_DESKTOP`, active) creates a desktop GL 3.3 core context via `SDL_GL_CreateContext`/`SDL_GL_SwapWindow` (GLon12-capable); **Xbox console** (`_GAMING_XBOX`, GDKX-gated fallback) uses a manual EGL/ANGLE GLES2 context + GDK PLM lifecycle |
| macOS helper | `macos_window.mm` (window activation for Steam launch) |

### Object Inheritance

All game entities inherit from a common base:

```
Object                          — position, velocity, radius, collision, step()
└── CompositeObject             — owns child Objects (e.g. asteroid fragments)
    ├── Ship                    — player/enemy logic (weapons, health, behaviours)
    │   ├── Enemy               — AI-controlled ship (targeting, difficulty level)
    │   └── GLStation           — enemy station; deploys waves of GLEnemy ships
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

- World grows by 50×50 per generation; at generation 20 it instead grows by 3000×3000
- Asteroid count: `default_num_asteroids + generation * extra_num_asteroids`
- Special asteroid types unlock by generation: reflective ≥ 2, teleporting ≥ 3, invisible ≥ 4, quantum ≥ 5, tough ≥ 6, armoured ≥ 7, phasing ≥ 8 (counts scale with generation)
- Black hole spawns at world centre from generation ≥ 9
- `GLStation` (enemy station) spawns from generation ≥ 20
- Pickups are cleared, the starfield and grid are rebuilt, players respawn, and progress is auto-saved

## Key Systems

### State Machine

`StateManager` (`state_manager.h/cpp`) drives top-level transitions. Each state inherits from `State` (`state.h/cpp`):

- Pure virtual: `draw()`, `keyboard()`, `keyboard_up()`, `controller()`, `tick()`
- Virtual: `touch_tap()`, `back_pressed()`, `resize()`
- States call `request_state_change()` to transition

There are two states:
- **Menu** (`menu.h/cpp`) — main menu and options screen, animated starfield, touch support. Options (5 steps each): P1/P2 sensitivity (SLOW–MAX, 0.5–2.0), P1/P2 camera smoothing (OFF–MAX, 0.0–0.010), star density (MINIMAL–FULL, 0.1–1.0 multiplier)
- **GLGame** (`glgame.h/cpp`) — in-game; owns all game objects; handles asteroid spawning, pickup drops, two-player split-screen, pause, auto-save. Game over transitions back to Menu (no separate game-over state)

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
| `extra_life` | Extra Life | +1 life (heart shape) |

**Drop chances** (per asteroid death, constants in `glgame.cpp`): extra_life 0.3125%, weapon 1.25%, mine 1.25%, giga_mine 0.5%, missile 1.25%, shield 1.25%, god_mode 0.25%.

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

### Save / Load

**Savegame** (`savegame.h/cpp`) — binary format, magic "NWTN", version 10:
- `WeaponEntry`: kind, weapon_index, ammo
- `Player`: score, lives, kills, respawning flag, position, velocity, facing, weapons, nova state
- `Asteroid`: position, velocity, radius, health, all special flags and transient state
- `Pickup`: type, position, weapon_index
- `BlackHole`, `Enemy`, `Station`: positional/state data (Station includes its deployed enemies)
- `MiniStation`: present flag, alive, position, drift velocity, rotations, shot timer
- `GameState`: generation, world size, level_cleared, players, all object lists

**Backward compatibility:** `GameState::MIN_VERSION..VERSION` all load; older or newer files are ignored. New fields are only ever **appended at the end** and read back gated on `version >= N`, so an older save stops short and the new fields take their defaults (e.g. v9 saves load with no mini-station). Loading then re-saving upgrades the file to the current `VERSION`. Keep this convention when bumping the version so existing saves survive.

Auto-save triggers on pause or player death if the player has lives or score remaining, and on level completion.

**Preferences** (`preferences.h/cpp`) — INI file in SDL pref path; global `g_prefs` instance:
- Per-player (`PlayerKeys`): 12 key bindings (left, right, thrust, shoot, reverse, mine, next_weapon, next_secondary, boost, teleport, help, toggle_rotate_view; defaults WASD + Space/X) plus `keyboard_sensitivity` and `camera_smoothing` floats
- General (`GeneralKeys`): pause (P), menu (Esc), add_player2 (Enter), toggle_friendly_fire (G), skip_level (N), toggle_debug_grid (B), time_speed_up (=), time_slow_down (-), time_reset (0), toggle_fullscreen (F)
- Display: `fullscreen` flag, `window_width`/`window_height`
- Camera: `rotate_view` flag
- Gameplay: `friendly_fire` flag, `star_density` multiplier (user-editable float in the INI)
- API: `load_preferences()`, `save_preferences()`; missing keys in old files are silently ignored

**High Score** (`highscore.h`) — `load_high_score()` / `save_high_score(score)`.

### Touch Controls

`touch_controls.h/cpp` — maps screen zones to ship actions for iOS/Android:
- Left half: virtual joystick (`joy_nx`, `joy_ny`, `joy_cx`, `joy_cy`, `joy_radius`)
- Right half: shoot / mine buttons
- `resize()` handler for dynamic layout

### Audio

SDL_mixer; all assets are WAV files in `audio/` (generated by `generate_sounds.py`). Mobile builds copy them into the app bundle/assets. God Mode weapon plays special music with a warning phase in the final 3 seconds.

## CI/CD

Three active GitHub Actions workflows drive the automatic PR/push checks — the self-hosted Xbox build, a hosted GLon12 build-and-run smoke, and a GDKX-free hosted console-API compile smoke. Every other build/deploy workflow is disabled (moved into `.github/workflows/disabled/`).

| Workflow | Output |
|----------|--------|
| `.github/workflows/xbox.yml` | GDK Desktop build (SDL-WGL + desktop GL renderer, GLon12-capable) on a **self-hosted** Windows runner (`runs-on: [self-hosted, windows]`). Ninja + MSVC, no GDK and no ANGLE: `cmake -B xbox/build-desktop -S xbox` builds standalone against SDL2/SDL2_mixer (FetchContent) + `opengl32`, statically linked (`/MT`) so `newtonia.exe` has no extra runtime DLLs. Host prereqs: VS 2022 Build Tools (C++), CMake/Ninja, and Git. Triggers on PRs and on pushes to `master`/`main` (feature branches build via their PR only, to avoid double runs). |
| `.github/workflows/windows-glon12.yml` | Hosted-runner (`windows-latest`) sibling of `xbox.yml`: builds the **same** GDK Desktop target with the default VS 2022 generator (no self-hosted box, no GDK, no ANGLE), then **runs the real game through Mesa's desktop GLon12** (OpenGL-on-D3D12) and greps `newtonia.log` for a `GL context:.*Mesa` line to prove the GLon12 path works end-to-end in CI. Tries `GALLIUM_DRIVER=d3d12`, falls back to `llvmpipe` (hosted runners have no GPU, so d3d12 can't get a WGL pixel format; both share the same Gallium GL frontend). Successor to the retired `windows-glon12-spike.yml` (which ran a standalone triangle probe, not the game). Same trigger convention as `xbox.yml`. |
| `.github/workflows/xbox-console-smoke.yml` | Compile-only check of the game against the **Xbox Game OS API partition** (`/DWINAPI_FAMILY=WINAPI_FAMILY_GAMES`) on a stock hosted runner — no GDK/GDKX, no NDA material, no link step. Catches forbidden-API usage (GDI/D3D11/desktop-GL/WGL) in the renderer + shared code before it reaches a dev kit. Defines `_GAMING_DESKTOP` so it compiles the **desktop-GL/GLon12 renderer** (`DESKTOP_COMPAT_GL`) — the console's decided path — **not** the abandoned ANGLE/GLES2 flow (which is `_GAMING_XBOX`-only and goes uncompiled here). Same trigger convention as `xbox.yml`. |

**Disabled workflows** — `.github/workflows/disabled/` holds all inactive workflows; move a file back into `.github/workflows/` to re-enable it:
- Builds: `macos-dev.yml`, `android.yml`, `ios.yml`, `linux.yml`, `windows.yml`, `web.yml`
- Xbox (hosted-runner): `xbox-dev.yml` (GDK Desktop / Gaming.Desktop.x64 canary) — reference for the GDK/ANGLE setup; superseded for the build matrix by the self-hosted `xbox.yml`
- `windows-glon12-spike.yml` — the GDKX-free GLon12 desktop rendering spike (`xbox/GLON12_SPIKE.md`, work-items 3a/4a). Concluded: GLon12 covers our GL 3.3 core feature set and the full game runs through it on real hardware (results recorded in `xbox/GLON12_SPIKE.md`); its `build-desktop` job duplicated `xbox.yml`'s self-hosted build, which now covers that ground on every PR. Superseded by the active `windows-glon12.yml`, which folds the full-game GLon12 run into a hosted PR build
- Deployment: `deploy-steam.yml`, `deploy-ios.yml`, `deploy-android.yml`, `deploy-itch.yml`, `deploy-macos.yml`, `deploy-windows.yml`, `deploy-xbox.yml`

**Steam integration** — `steam_build.h` (constants/SDK), `steam/` contains Steamworks VDF config files (`app_build.vdf`, `depot_build_windows.vdf`, `depot_build_macos.vdf`, `depot_build_linux.vdf`).

## Conventions & Patterns

1. **GL-prefix pattern** — Rendering wrappers (`GLShip`, `GLGame`, `GLEnemy`, `GLCar`) wrap pure logic classes. Never put rendering into logic classes. (`GLStation` is the one existing exception — it inherits `Ship` directly.)
2. **Weapon/pickup pairs** — Every weapon type has a corresponding `*_pickup` class at root level.
3. **Behaviour pattern** — Abstract `Behaviour` base with `done` flag; `Ship` owns a list and runs them each frame.
4. **State machine** — `StateManager` + `State` subclasses drive all top-level transitions.
5. **Serialization** — Major game objects implement `capture_state()` / `restore_state()` pairs.
6. **Grid collision** — Use `Grid` for all spatial queries; never iterate all objects.
7. **Mesh builder** — All geometry is pre-uploaded to GPU VBOs via `MeshBuilder`; no immediate-mode GL calls.
8. **Platform abstraction** — Use `gl_compat.h` macros; never call desktop-only GL functions directly.
9. **File naming** — Behaviours: `*_behaviour.h/cpp`. Weapons: `weapon/*.h/cpp`. Pickups: `*_pickup.h/cpp` at root. Views/HUD: `view/*.h/cpp`.
10. **C++11** — Codebase targets C++11 (`-std=c++11`). Avoid later standard features.
