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
`weapon_cycle`, `camera`, `keyboard_presses`, `touch_presses`,
`gamepad_active_samples`. Keyboard events query the game's live per-seat bindings
(including remaps, alternates and P2); held keys count once, not per repeat or shot.
Touch joystick movement counts neutral-to-deflected transitions (not distance).
Canvas fallback controls count finger-downs, not zone crossings during a drag.
Direct canvas pause and zoom taps are included in `pause` and `camera`.
`pause` counts successful local player pause/resume transitions, including
Enter on RESUME and resume from the roster, rather than attempted keys/taps.
Automatic focus/disconnect/help-card changes, remote changes and replay controls
are excluded. Pause transitions do not increment a device press counter because
the shared transition does not infer whether its caller was keyboard, touch or pad.
Gamepads are sampled at 4 Hz; only standard-mapped stick axes use a 0.25 dead zone
(unknown mappings use buttons only, avoiding idle trigger axes at -1), counting at most one
active sample per tick across four pads; short taps can be missed. These are
control intentions, not confirmed shots, successful abilities or gameplay time.

Collection requires the production hostname `newtonia.metonymous.com` and a
focused/visible document. **itch.io embeds and local builds are intentionally
excluded** from this site's metrics. Visits with a `replay` query parameter are
excluded entirely. The read-only game-state query gates on the current live
GLGame, including online host/join, and excludes menus, intros, replay,
spectating and blocking overlays. While paused, only successful player resume
transitions are counted; a touch consumed by CONTROLS does not count as resume.
It is queried on input / at 4 Hz, never
pushed per rendering frame. Buffered counts still flush after leaving play.
These events do not measure active gameplay duration. Native builds send no
analytics. No raw keys, names, coordinates, controller IDs or player IDs are
added. The shell marks readiness after the existing GA script loads; bounded
counters remain in memory until then and flush on the readiness event or next
timer/lifecycle flush, without filling the inline tag stub or inspecting
`dataLayer.length`. Leaving the page before the tag can load still prevents
delivery; counters are not persisted across visits. Errors in sending do not
affect gameplay.

In GA4, use the Users metric on `game_controls_used` for visitors who used
controls (its event count is page lifetimes, not unique users). Register the
eleven summary parameters as event-scoped custom metrics to report their sums;
the summary event count alone counts batches, not actions. Definitions are not
created automatically by this code. The existing shell loads/configures GA4 unconditionally; there is no consent
banner or consent-mode gate. This change does not add or claim such a gate.

Regression checks: `node test/unit/web_analytics.cjs` and
`node test/unit/touch_one_hand_web.cjs`. Both compile production TypeScript.
