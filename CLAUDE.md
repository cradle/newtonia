# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Newtonia is a top-down 2D space shooter written in C++ using SDL2 and OpenGL. It supports single-player and two-player modes and targets multiple platforms: desktop (macOS, Linux, Windows), mobile (iOS, Android), web (WebAssembly), and Steam.

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

### Web (Emscripten)
```sh
make web        # Build WebAssembly output to web/dist/
make web-clean  # Remove web build artifacts
```

Requires Emscripten (`emcc`) and TypeScript compiler (`tsc`) on PATH. Web build links `-lidbfs.js` for IndexedDB persistence and preloads audio assets.

### Android
```sh
cd android && ./gradlew assembleDebug
```

SDL2 and SDL2_mixer are cloned from GitHub by the CMake build. Requires Android NDK 26.3.11579264 and CMake 3.22.1. The CMake target is `libnewtonia.so` (shared library). Source roots: `weapon/` and `view/` are included alongside root files.

### iOS
Open `ios/Newtonia-iOS.xcodeproj` in Xcode. For simulator builds see `ios/README.md`.

## Architecture

### Class Hierarchy

The codebase separates **game logic** from **rendering** using a GL-prefixed wrapper pattern:

- `Ship` / `Asteroid` / `Pickup` / `Enemy` — pure game logic (physics, health, state)
- `GLShip` / `GLEnemy` / `GLCar` / `GLStarfield` / `GLStation` — rendering + input layer wrapping logic classes
- `GLGame` — top-level in-game state, owns all GL* objects, drives the update/draw loop

### Platform Entry Points

| Platform | Entry point |
|----------|-------------|
| Desktop  | `glut.cpp` (GLUT main loop) |
| Android  | `android_main.cpp` (SDL2 event loop, multi-touch) |
| iOS      | `ios/ios_main.mm` |
| Web      | `web_main.cpp` (Emscripten loop, IDBFS persistence) |
| macOS helper | `macos_window.mm` (window activation for Steam launch) |

### Object Inheritance

All game entities inherit from a common base:

```
Object                          — position, velocity, radius, collision, step()
└── CompositeObject             — owns child Objects (e.g. asteroid fragments)
    ├── Ship                    — player/enemy logic (weapons, health, behaviours)
    │   └── Enemy               — AI-controlled ship (targeting, difficulty level)
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
- World size: 2500×2500 units (set via `WrappedPoint::set_boundaries`)

## Key Systems

### State Machine

`StateManager` (`state_manager.h/cpp`) drives top-level transitions. Each state inherits from `State` (`state.h/cpp`):

- Pure virtual: `draw()`, `keyboard()`, `keyboard_up()`, `controller()`, `tick()`
- Virtual: `touch_tap()`, `back_pressed()`, `resize()`
- States call `request_state_change()` to transition

Known states:
- **Menu** (`menu.h/cpp`) — main menu, options screen (keybindings, sensitivity, camera), animated starfield, touch support
- **GLGame** (`glgame.h/cpp`) — in-game; owns all game objects; handles asteroid spawning, pickup drops, two-player split-screen, pause, auto-save
- Game over / high score states (managed by StateManager)

### Weapon System

`Weapon::Base` (`weapon/base.h`) defines the interface: `shoot()`, `step()`, ammo tracking. Each weapon has its own `.h`/`.cpp` in `weapon/`:

| File | Weapon | Notes |
|------|--------|-------|
| `weapon/default` | Default gun | Automatic/semi-auto; `level` controls accuracy; `time_between_shots` |
| `weapon/mine` | Mine | Deployable, limited ammo, large blast |
| `weapon/giga_mine` | Giga Mine | Larger blast than mine |
| `weapon/missile` | Missile | Homing AI; `set_asteroids()` / `set_ship_targets()`; seeks via `query_segment()` |
| `weapon/shield` | Shield | Energy barrier, limited ammo |
| `weapon/god_mode` | God Mode | Timed invincibility; fires periodic shockwaves (150ms); plays special music |
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

**Drop chances** (per asteroid death): extra_life 0.3%, weapon 1.25%, mine 1.25%, giga_mine 0.5%, missile 1.25%, shield 1.25%, god_mode 0.25%.

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
| `elastic` | Bounces off other elastic asteroids |
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

**Grid** (`grid.h/cpp`) provides spatial partitioning. Objects register themselves each frame; `collide()` finds neighbors; `query_segment()` supports line queries (missile homing). Fixed-radius proximity checks via `Object::collide()`.

### Coordinate System

- **Point** (`point.h/cpp`) — 2D vector (x, y)
- **WrappedPoint** (`wrapped_point.h/cpp`) — toroidal wrapping; objects exiting one edge reappear on the opposite side

### Rendering

**OpenGL compatibility** — `gl_compat.h` and `gles2_compat.h/cpp` abstract desktop OpenGL 3.3 Core vs OpenGL ES 2 (mobile/web). `gles2_compat` provides GLSL program wrappers, vertex/color buffers, and line-thickening for WebGL (which disallows `lineWidth > 1`).

**Mesh system** (`mesh.h/cpp`) — GPU geometry with interleaved position/colour data. Builder API:
```cpp
MeshBuilder::begin(mode) → color() → vertex() → end() → Mesh
mesh.upload(); mesh.draw(); mesh.draw_tinted(); mesh.draw_at(); mesh.draw_with_model();
```

**Text** — `Typer` (`typer.h/cpp`): static `draw()`, `draw_centered()`, `draw_lefted()`, `draw_lives()`. Per-character GPU meshes; dynamic window resize.

**WarpPass** (`warp_pass.h/cpp`) — gravitational-lens distortion shader for invisible asteroids. Captures viewport to texture, applies radial-distortion pass. Works on both GLSL 1.50 (desktop) and GLSL ES 1.00 (mobile/web).

**Particles** — `Particle` (`particle.h/cpp`): `Object` subclass with TTL countdown; `GLTrail` (`gltrail.h/cpp`) renders thruster particle trails. A particle can be flagged `kills_invincible` to penetrate shields.

**Split-screen** — `GLGame` renders into multiple viewports for two-player mode. `view/overlay.cpp` handles all HUD elements: score, lives, weapons, temperature, level, respawn timer, keymap, god-mode indicator, touch controls, edge indicators, debug info.

**Starfield** — `GLStarfield` (`glstarfield.h/cpp`): parallax background with layered star densities.

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

`GLShip` (`glship.h/cpp`) — rendering + input wrapper: keyboard, controller (SDL_GameController with analog sticks), touch joystick. Camera following with optional rotation (`toggle_rotate_view`). Smooth camera follow (configurable rate).

`GLEnemy` (`glenemy.h/cpp`) — rendering wrapper for `Enemy`.

`GLCar` (`glcar.h/cpp`) — alternative player ship model with left/right jets.

### Save / Load

**Savegame** (`savegame.h/cpp`) — binary format, magic "NWTN", version 9:
- `WeaponEntry`: kind, weapon_index, ammo
- `Player`: score, lives, kills, respawning flag, position, velocity, facing, weapons, nova state
- `Asteroid`: position, velocity, radius, health, all special flags and transient state
- `Pickup`: type, position, weapon_index
- `BlackHole`, `Enemy`, `Station`: positional/state data
- `GameState`: generation, world size, level_cleared, players, all object lists

Auto-save triggers on pause or player death if the player has lives or score remaining.

**Preferences** (`preferences.h/cpp`) — INI file in SDL pref path; global `g_prefs` instance:
- Per-player keyboard bindings (11 keys + 2 analog sensitivity settings); defaults: WASD + Space/X
- General: pause (P), menu (Esc), add_player2 (Enter)
- Display: fullscreen flag, window resolution
- Camera: `rotate_view` flag
- Gameplay: `friendly_fire` flag
- API: `load_preferences()`, `save_preferences()`

**High Score** (`highscore.h`) — `load_high_score()` / `save_high_score(score)`.

### Touch Controls

`touch_controls.h/cpp` — maps screen zones to ship actions for iOS/Android:
- Left half: virtual joystick (`joy_nx`, `joy_ny`, `joy_cx`, `joy_cy`, `joy_radius`)
- Right half: shoot / mine buttons
- `resize()` handler for dynamic layout

### Audio

SDL_mixer; WAV files for effects, MP3 for music. All assets live in `audio/`. Mobile builds copy into the app bundle/assets. God Mode weapon plays special music with a warning phase.

## CI/CD

GitHub Actions runs builds on every push to `master`, `main`, or `claude/*` branches, and on PRs:

| Workflow | Output |
|----------|--------|
| `.github/workflows/macos-dev.yml` | Universal arm64+x86_64 binary |
| `.github/workflows/android.yml` | Debug APK |
| `.github/workflows/ios.yml` | iOS simulator build |
| `.github/workflows/linux.yml` | Linux executable |
| `.github/workflows/windows.yml` | Windows executable |
| `.github/workflows/web.yml` | WebAssembly + GitHub Pages deploy (master/main only) |

**Deployment workflows** (triggered manually):
- `.github/workflows/deploy-steam.yml` — Steam (Windows/macOS/Linux via Steamworks SDK)
- `.github/workflows/deploy-ios.yml` — TestFlight
- `.github/workflows/deploy-android.yml` — Play Store
- `.github/workflows/deploy-itch.yml` — Itch.io

**Steam integration** — `steam_build.h` (constants/SDK), `steam/` contains Steamworks VDF config files (`app_build.vdf`, `depot_build_windows.vdf`, `depot_build_macos.vdf`, `depot_build_linux.vdf`).

## Conventions & Patterns

1. **GL-prefix pattern** — Rendering wrappers (`GLShip`, `GLGame`, `GLEnemy`, `GLCar`) wrap pure logic classes. Never put rendering into logic classes.
2. **Weapon/pickup pairs** — Every weapon type has a corresponding `*_pickup` class at root level.
3. **Behaviour pattern** — Abstract `Behaviour` base with `done` flag; `Ship` owns a list and runs them each frame.
4. **State machine** — `StateManager` + `State` subclasses drive all top-level transitions.
5. **Serialization** — Major game objects implement `capture_state()` / `restore_state()` pairs.
6. **Grid collision** — Use `Grid` for all spatial queries; never iterate all objects.
7. **Mesh builder** — All geometry is pre-uploaded to GPU VBOs via `MeshBuilder`; no immediate-mode GL calls.
8. **Platform abstraction** — Use `gl_compat.h` macros; never call desktop-only GL functions directly.
9. **File naming** — Behaviours: `*_behaviour.h/cpp`. Weapons: `weapon/*.h/cpp`. Pickups: `*_pickup.h/cpp` at root. Views/HUD: `view/*.h/cpp`.
10. **C++11** — Codebase targets C++11 (`-std=c++11`). Avoid later standard features.
