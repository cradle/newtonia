# GLon12 Desktop Rendering Spike

Phase 2 / work-item 3a of `xbox/PORT_PLAN.md`. **GDKX-free** — runs on any
Windows box (or hosted CI), no GDK and no dev kit.

## The question

The Xbox port's primary rendering candidate (Option A) is **GLon12** — Mesa's
OpenGL-on-D3D12 Gallium driver — driven through SDL2's WGL backend, reusing
Newtonia's existing desktop **GL 3.3 core** renderer instead of the GLES2/ANGLE
path. The biggest open risk is:

> Does GLon12 expose a GL version and feature set that covers our full GL 3.3
> core renderer, and does the renderer's GLSL-330 program actually compile,
> link, and rasterise through it?

Most of that is answerable **without GDKX**. Mesa ships a *desktop* GLon12 build
(`opengl32.dll` + `libgallium_wgl.dll` + `dxil.dll`) that runs the same Gallium
`d3d12` driver on a normal Windows PC. Dropping those DLLs next to a desktop GL
program makes its WGL context a D3D12-backed GLon12 context, exercising the same
GL frontend + d3d12 driver the console will use. Only three things genuinely
need GDKX (deferred — see "What this does NOT prove"):

1. Building Mesa's d3d12 driver for the **Xbox cross target**.
2. Confirming the **Xbox Game OS feature level** doesn't lower the exposed GL
   version below what the desktop spike measures.
3. Running on the **dev kit**.

## What's here

| File | Purpose |
|------|---------|
| `xbox/glon12/glon12_probe.cpp` | Standalone SDL2 + GL 3.3 core probe. Creates a real WGL context via `SDL_GL_CreateContext` (the console path — *not* ANGLE/EGL), loads the exact GL entry points the renderer needs (`COMPAT_GL_FNS` from `gles2_compat.cpp`), compiles/links a representative GLSL-330 program, draws a triangle, reads back a pixel to prove rasterisation, and reports PASS/FAIL. |
| `xbox/glon12/CMakeLists.txt` | Builds the probe (finds an installed SDL2 or fetches the pinned one). |
| `xbox/glon12/run_spike.ps1` | Windows: build + run baseline (system GL) and GLon12 runs, given `-MesaDir`. |
| `.github/workflows/windows-glon12.yml` | Hosted-runner CI: build, fetch Mesa desktop GLon12, run under `GALLIUM_DRIVER=d3d12`. Runs on the `claude/glon12-spike-**` branch and on manual dispatch (it does not join the automatic PR/push matrix — that stays the single `xbox.yml`). |

The probe mirrors the **target console path** deliberately: SDL2 WGL +
`SDL_GL_CreateContext`/`SDL_GL_SwapWindow` + GL 3.3 core, which is also
work-item #4 (switch the console SDL2 build off the ANGLE/EGL-pbuffer path).

## How to run

### On any Windows box

1. Get Mesa's desktop GLon12 DLLs — the "x64 desktop" set from a
   [`pal1000/mesa-dist-win`](https://github.com/pal1000/mesa-dist-win/releases)
   release contains `opengl32.dll`, `libgallium_wgl.dll`, `dxil.dll`.
2. ```powershell
   pwsh xbox/glon12/run_spike.ps1 -MesaDir C:\path\to\mesa\x64
   ```
   It builds the probe, runs it once against the system driver (baseline), then
   again with the Mesa DLLs staged beside the exe and `GALLIUM_DRIVER=d3d12`.

### In CI

`.github/workflows/windows-glon12.yml` runs automatically on the
`claude/glon12-spike-**` branch and can be triggered manually (Actions →
"Windows GLon12 spike" → Run, with an optional Mesa version input). It fetches
Mesa, runs the baseline + GLon12 probes, and uploads `glon12_probe.log`.

### Cross-platform sanity build

The probe compiles and runs on Linux/macOS against the system GL too (no GLon12
there — it just reports the system backend). Useful to validate the probe logic:
```sh
cmake -B xbox/glon12/build -S xbox/glon12 && cmake --build xbox/glon12/build
```

## Reading the result

Exit code `0` / `RESULT: PASS` means, for the GL backend behind the context:
context is ≥ GL 3.3 core, all required entry points resolved, the GLSL-330
program compiled + linked, and the triangle rasterised (centre pixel non-empty,
no GL error). Non-zero exit codes pinpoint the failed check (see the source
header). `Backend: GLon12 (D3D12)` in the log confirms the run actually went
through GLon12 rather than falling back to the system/llvmpipe driver.

## Results log

| Date | Host | Backend (GL_RENDERER) | GL / GLSL | Entry pts | Shader | Triangle | Result |
|------|------|-----------------------|-----------|-----------|--------|----------|--------|
| 2026-06-15 | Linux CI container | `llvmpipe (LLVM 20.1.2)` (Mesa Gallium, offscreen) | 4.5 / 4.50 | 29/29 | OK | OK | **PASS** (probe validation) |
| _TODO_ | Windows + Mesa GLon12 | `D3D12 (...)` | _?_ | _?_ | _?_ | _?_ | _pending_ |
| _TODO_ | Xbox dev kit (GDKX) | _?_ | _?_ | _?_ | _?_ | _?_ | _deferred_ |

The Linux row validates the probe itself and is a useful early signal: it runs
through the **same Mesa Gallium GL frontend** as the d3d12 driver, and that
frontend already exposes our full feature set (29/29 entry points, GLSL ≥ 330,
triangle rasterised). The d3d12 row replaces the software rasteriser with the
D3D12 backend; the dev-kit row is the only one that needs GDKX.

## What this does NOT prove

- **Xbox Game OS feature level.** The desktop d3d12 driver runs against desktop
  D3D12; the console runs `D3D12.x` under a restricted feature level. A GL
  version/extension exposed on desktop GLon12 could in principle differ on
  console. Re-run the equivalent probe on the dev kit (GDKX) to confirm.
- **The Xbox build of Mesa.** GLon12-for-Xbox must be built with
  `-Dgallium-drivers=d3d12` for the Xbox cross target and is gated behind a
  Microsoft agreement; it does not work with the public PC GDK.
- **SDL2's GDK windowing.** The probe uses SDL2's plain Win32 WGL backend. On
  console the WGL backend goes through SDL's `VisualC-GDK` Game OS path
  (`GetDC` remap, `wgl*` via `SDL_LoadFunction`, supplied `PIXELFORMATDESCRIPTOR`).
  That plumbing is validated only on the dev kit.
- **Full-game performance.** This is a single-triangle feature probe, not a
  frame-budget test; that belongs to Phase 3 on hardware.
