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

Dependencies: SDL2 and SDL2_mixer must be installed (`brew install sdl2 sdl2_mixer` on macOS).

### Web (Emscripten)
```sh
make web        # Build WebAssembly output to web/dist/
make web-clean  # Remove web build artifacts
```

Requires Emscripten (`emcc`) and TypeScript compiler (`tsc`) on PATH.

### Android
```sh
cd android && ./gradlew assembleDebug
```

SDL2 and SDL2_mixer are cloned from GitHub by the CMake build. Requires Android NDK 26.3.11579264 and CMake 3.22.1.

### iOS
Open `ios/Newtonia-iOS.xcodeproj` in Xcode. For simulator builds see `ios/README.md`.

## Architecture

### Class Hierarchy

The codebase separates **game logic** from **rendering** using a GL-prefixed wrapper pattern:

- `Ship` / `Asteroid` / `Pickup` — pure game logic (physics, health, state)
- `GLShip` / `GLStarfield` / `GLStation` / `GLEnemy` — rendering + input layer wrapping the logic classes
- `GLGame` — top-level game state, owns all GL* objects, drives the update/draw loop

### Platform Entry Points

| Platform | Entry point |
|----------|-------------|
| Desktop  | `glut.cpp` (GLUT main loop) |
| Android  | `android_main.cpp` |
| iOS      | `ios/ios_main.mm` |
| Web      | `web_main.cpp` |

### Object Inheritance

All game entities inherit from a common base:

```
Object                          — position, velocity, radius, collision, step()
└── CompositeObject             — owns child Objects (e.g. asteroid fragments)
    ├── Ship                    — player/enemy logic (weapons, health, behaviours)
    └── Asteroid                — breakup mechanics, spawns children on death
Pickup (: Object)               — collectible items dropped by asteroids
```

`Object` provides `step(delta)`, `collide()`, `kill()`, and `is_removable()`. Subclasses override these as needed.

### Behaviours

`Behaviour` is an abstract base (`step(delta) = 0`) that holds a pointer to a `Ship` and sets `done = true` when finished. Ships run a list of active behaviours each frame (e.g. `ShieldBehaviour`). Each behaviour gets its own `.h`/`.cpp` file named `*_behaviour`.

### Key Systems

**Collision detection** — `Grid` provides spatial partitioning. Objects register themselves and query neighbors each frame.

**Weapon system** — `Weapon::Base` (`weapon/base.h`) defines the interface (`shoot()`, `step()`, ammo tracking). Each weapon gets its own `.h`/`.cpp` file in `weapon/`: `default`, `mine`, `giga_mine`, `missile`, `shield`, `god_mode`. Each weapon also has a corresponding `*_pickup` class (root level `*_pickup.h`/`.cpp`) that is the collectible dropped by asteroids.

**State machine** — `StateManager` drives top-level transitions (title → game → death → high score, etc.). Each state inherits from `State`.

**Coordinate system** — `WrappedPoint` handles toroidal world wrapping so objects that exit one edge reappear on the opposite side.

**Audio** — SDL_mixer; WAV files for effects, MP3 for music. All assets live in `audio/`. Mobile builds copy these into the app bundle/assets.

**Touch controls** — `touch_controls.cpp` maps screen zones to ship actions (thrust, rotate, fire) for iOS/Android.

**OpenGL compatibility** — `gl_compat.h` and `gles2_compat.cpp` abstract differences between desktop OpenGL and OpenGL ES 2 (used on mobile/web).

### Rendering

The game supports split-screen two-player by rendering into multiple viewports. `view/overlay.cpp` handles HUD rendering. `GLTrail` renders thruster particle effects. Text is drawn via `Typer`.

## CI/CD

GitHub Actions runs builds on every push to `master`, `main`, or `claude/*` branches, and on PRs:
- `.github/workflows/macos.yml` — universal arm64+x86_64 binary
- `.github/workflows/android.yml` — debug APK
- `.github/workflows/ios.yml` — iOS simulator build
- `.github/workflows/linux.yml` — Linux executable
- `.github/workflows/windows.yml` — Windows executable
- `.github/workflows/web.yml` — WebAssembly + GitHub Pages deploy (master/main only)

Deployment workflows (TestFlight, Play Store, Steam) are triggered manually.
