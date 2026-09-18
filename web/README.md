# Newtonia — Web Port

Compiles the game to WebAssembly + WebGL via [Emscripten](https://emscripten.org).
The core C++ codebase is unchanged; only a new entry point (`web_main.cpp`) is added.

## Prerequisites

- **Emscripten SDK** — provides `emcc`
- **TypeScript compiler** — for the UI chrome (`web/main.ts`)
- **Python 3** — to serve the output locally (WASM requires HTTP)

## Build

```sh
# 1. Install Emscripten (once)
brew install emscripten      # or use emsdk

# 2. Compile TypeScript UI (once, or whenever web/main.ts changes)
cd web && tsc && cd ..

# 3. Build WebAssembly + HTML
make web
```

Output lands in `web/dist/`:

```
web/dist/
├── index.html        # marketing landing page (web/site/)
├── styles.css
├── site.js
├── icon.png
└── play/             # the playable WebAssembly game
    ├── index.html
    ├── index.js
    ├── index.wasm
    ├── index.data    # preloaded audio assets
    └── main.js
```

The landing page (`web/site/`) is served at the site root; the game lives at
`/play/`. Edit the landing page in `web/site/` — it is copied verbatim into
`web/dist/` by `make web` (no build step required for the static HTML/CSS/JS).

## Run locally

WASM cannot be loaded from `file://` due to browser security restrictions.
Serve the output directory over HTTP:

```sh
python3 -m http.server 8080 --directory web/dist
```

Then open <http://localhost:8080> for the landing page, or
<http://localhost:8080/play/> to jump straight into the game.

## Audio assets

The game loads these files at runtime:

| File | Used by |
|------|---------|
| `shoot.wav` | Default weapon |
| `empty.wav` | Empty clip click |
| `mine.wav` | Mine deploy |
| `tic.wav` | Ship heat warning |
| `tic_low.wav` | Ship heat warning (low) |
| `click.wav` | UI / ship events |
| `boost.wav` | Thruster boost |
| `explode.wav` | Asteroid explosion |
| `thud.wav` | Asteroid collision |
| `title.mp3` | Menu music |

Place them in a `sounds/` directory at the repo root, then uncomment this line in the Makefile and rebuild:

```makefile
# WEB_FLAGS += --preload-file sounds@/
```

Without them, `Mix_LoadWAV` returns NULL and audio is silently skipped — the game still runs.

## Controls

| Key | Action |
|-----|--------|
| `W` | Thrust |
| `S` | Reverse |
| `A` / `D` | Rotate |
| `Space` | Shoot |
| `X` | Secondary weapon |
| `Enter` | Start / confirm |
| `F` | Toggle fullscreen |

On touch devices, on-screen control zones appear automatically.

## Clean

```sh
make web-clean   # removes web/dist/
```

## Web control analytics

The existing GA4 tag receives `game_controls_used` once per page lifetime
with control activity, and `game_controls_summary` containing aggregate counts
every 30 seconds with activity, or when the page is hidden/exited. Delivery on
exit is best-effort. No network requests or GA calls run in input handlers or
the rendering loop. Input handlers only maintain bounded counters/held state.

Summary parameters: `move`, `fire`, `secondary`, `boost`, `teleport`, `pause`,
`keyboard_presses`, `touch_presses`, `gamepad_active_samples`. Keyboard events
use the default web key mapping; held keys count once, not per repeat or shot.
Touch joystick movement counts neutral-to-deflected transitions (not distance).
Gamepads are sampled at 4 Hz with a 0.25 axis dead zone, counting at most one
active sample per tick across four pads; short taps can be missed. These are
control intentions, not confirmed shots, successful abilities or gameplay time.

Collection requires the production hostname, focused/visible document and
non-menu UI. Visits with a `replay` query parameter are excluded entirely.
The menu bridge is not a precise simulation-running signal (e.g. pause/intro),
so these events must not be presented as measured active play duration. Native
Steam/iOS/Android controls are unaffected. No raw keys, names, coordinates,
controller identifiers or player identifiers are added. GA blocking/failures
do not affect input, and queued analytics is bounded by dropping summaries
when the existing dataLayer has 1,000 entries.

In GA4, use the Users metric on `game_controls_used` for visitors who used
controls (its event count is page lifetimes, not unique users). Register the
nine summary parameters as event-scoped custom metrics to report their sums;
the summary event count alone counts batches, not actions. Definitions are not
created automatically by this code. The tag's existing consent setup applies.

Regression checks: `node test/unit/web_analytics.cjs` and
`node test/unit/touch_one_hand_web.cjs`. Both compile production TypeScript.
