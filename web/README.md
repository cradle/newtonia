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

Output lands in `web/dist/`: `index.html`, `index.js`, `index.wasm`.

## Run locally

WASM cannot be loaded from `file://` due to browser security restrictions.
Serve the output directory over HTTP:

```sh
python3 -m http.server 8080 --directory web/dist
```

Then open <http://localhost:8080> in your browser.

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
| `X` | Deploy mine |
| `Enter` | Start / confirm |
| `F` | Toggle fullscreen (desktop) |

On touch devices, on-screen control zones appear automatically.

## Clean

```sh
make web-clean   # removes web/dist/
```
