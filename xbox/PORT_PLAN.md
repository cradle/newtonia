# Xbox Port — Implementation Plan

Status: planning document. Companion to `xbox/CMakeLists.txt`, `xbox_main.cpp`,
`.github/workflows/xbox-dev.yml`, and the disabled
`.github/workflows/disabled/deploy-xbox.yml`.

## 1. Where the port stands today

### Proven (builds in CI, code reviewed)

| Area | State |
|------|-------|
| GDK Desktop build (`Gaming.Desktop.x64`) | Compiles in CI on every push (`xbox-dev.yml`); artifact includes exe + ANGLE DLLs + audio |
| Entry point | `xbox_main.cpp` handles both `_GAMING_DESKTOP` and `_GAMING_XBOX`; SDL2 event loop, manual EGL/ANGLE context |
| Controller-only operation | Complete. Menu/options fully navigable by pad (`menu.cpp:291-409`); all ship actions mapped (`glship.cpp:325-449`); pause = START, quit-to-menu = BACK, add player 2 = START (`glgame.cpp:1448-1562`); hot-plug + per-player assignment in `state_manager.cpp:37-60`; auto-pause on controller disconnect (`glgame.cpp:447-455`) |
| Help overlay | Switches to controller glyphs automatically (`glship.cpp:646-794`) |
| Rendering abstraction | Xbox/GDK takes the GLES2 path (`gl_compat.h:15-17`, `gles2_compat.h:58-73`) — same code as Android/iOS/Web, no desktop-GL leakage |
| Save / prefs / highscore | All via `SDL_GetPrefPath()` + `fopen` — maps to GDK LocalState; no hardcoded paths |
| Audio | Relative `audio/` paths, staged next to exe by CMake post-build; 48 kHz mixer matches Xbox |
| Suspend handling | `focus_lost()` saves progress + pauses (`glgame.cpp:409-416`); Xbox PLM suspend callback wired (`xbox_main.cpp:163-169`) |
| Packaging scaffolding | `MicrosoftGame.config`, `PackagingLayout.xml`, `deploy-xbox.yml` (disabled) with makepkg + StoreBroker steps |

### Untested / known-wrong (the actual port work)

1. **Every `_GAMING_XBOX` code path has never been compiled or run.** Only
   the Desktop target builds in CI.
2. **ANGLE on the console is an unvalidated assumption — the biggest risk.**
   `ANGLE.WindowsStore` (the NuGet the workflows use) is a 2017-era UWP/x64
   Windows binary built on desktop D3D11/DXGI. Xbox Series GDK titles must
   render through D3D12.X (or D3D11.X), whose headers/libs ship only in the
   NDA "GDK with Xbox extensions" (GDKX). There is no official ANGLE build
   for Xbox consoles. The console rendering strategy needs a decision spike
   (§3, Phase 2).
3. **SDL2 console windowing is unvalidated.** The build compiles SDL2 with
   `/U__GDK__` so it uses its plain Win32 backend. That works on GDK Desktop
   (Win32-compatible) but the console exposes a restricted API surface; SDL2's
   official Xbox support (2.24+) goes through its `VisualC-GDK` projects and
   requires GDKX. `xbox_main.cpp`'s console path also assumes
   `SDL_GetWindowWMInfo` yields an HWND usable with `eglCreateWindowSurface`.
4. **The public GDK does not include the Scarlett platform.** The winget
   `Microsoft.Gaming.GDK` package is the GRDK (Desktop only). The
   `Gaming.Xbox.Scarlett.x64` MSBuild platform, D3D12.X, and the console
   toolchain come with GDKX, which is NDA-distributed to ID@Xbox members and
   must not be uploaded to public CI runners. The disabled `deploy-xbox.yml`
   as written (Scarlett configure on a hosted runner) cannot work.
5. **Hardware access required.** Retail-console Developer Mode runs UWP apps
   only; GDK packages need an ID@Xbox dev kit. There is no way to runtime-test
   the console build before Phase 0 completes.
6. **Packaging details inconsistent.** `MicrosoftGame.config` /
   `PackagingLayout.xml` say `TargetDeviceFamily="XboxOne"` while the build
   targets Scarlett; identity/StoreId fields are TODO placeholders;
   `xbox/Assets/` (StoreLogo, tiles, splash) doesn't exist. StoreBroker is an
   appx/MSIX-era tool — whether it can submit GDK `.xvc` packages needs
   verification (console packages are normally uploaded via Partner Center /
   the game package upload flow).
7. **No TV safe-area handling.** No safe-area inset exists anywhere in the
   HUD (`view/overlay.cpp`, `Typer`). Xbox cert guidance requires critical UI
   inside the title-safe region (~90% of the frame).
8. **Doc bug:** `xbox/CMakeLists.txt:33-34` claims ANGLE is bundled with the
   GDK; the workflows themselves note it is not. Fix the comment when touched.

## 2. Target definition

- **Primary target:** Xbox Series X|S (`Gaming.Xbox.Scarlett.x64`), single
  package. Xbox One support is a non-goal unless ID@Xbox requires it (the
  game is light enough that an XboxOne-family package is feasible later).
- **GDK Desktop** stays as the free CI canary and PC test vehicle, not a
  shipping target (Steam covers Windows).
- **Features:** offline 1–2 player couch play, exactly as on desktop. No
  Xbox Live multiplayer, achievements, or leaderboards in v1. Sign-in and
  save handling only to the extent certification requires.

## 3. Phases

### Phase 0 — Program prerequisites (calendar time, little code)

Blocking everything console-side; start immediately.

1. Enrol in ID@Xbox (https://developer.microsoft.com/games). Approval can
   take weeks.
2. On approval: Partner Center title creation → real `Identity/@Name`,
   `@Publisher`, `StoreId` for `MicrosoftGame.config`.
3. Obtain GDKX access + request a dev kit loan.
4. Decide publisher display name, age ratings (IARC questionnaire), pricing.

Exit criteria: GDKX installable on a dev machine; dev kit on the desk.

### Phase 1 — Harden GDK Desktop (no new hardware needed, start now)

The Desktop build compiles but is largely runtime-unvalidated.

1. Manual test pass of the CI artifact on a Windows machine: boot, menu,
   options persistence, 1P/2P with pads, pause/focus loss, save/resume,
   fullscreen toggle, window resize, quit. Log via `newtonia.log`.
2. Fix whatever that pass shakes out (likely candidates: pbuffer blit
   performance at large window sizes — `glReadPixels` + `StretchDIBits` is
   CPU-bound; acceptable for a dev vehicle, document as Desktop-only).
3. Polish: hide keyboard-only cheat rows (skip level, time controls, debug
   grid, fullscreen) from the help overlay when `last_input_was_controller`
   (`glship.cpp:759-794`) — they're unreachable and confusing on pad.
4. Verify suspend→resume→save integrity by minimising/restoring mid-game.

Exit criteria: Desktop build is a trustworthy reference for "the game logic
and GLES2 renderer are correct on Windows/ANGLE".

### Phase 2 — Console rendering decision spike (needs GDKX)

Decide how GLES2 calls reach the console GPU. Timebox ~1–2 weeks of
investigation before committing.

- **Option A — compile ANGLE against GDKX D3D11.X/D3D12.** Keeps the
  renderer untouched. Unknown effort; no official support, and ANGLE's D3D
  backends assume desktop DXGI swap chains. Investigate first; abandon
  quickly if swap-chain/device creation can't be adapted.
- **Option B — native D3D12.X backend behind the existing abstraction.**
  The game's GPU usage is deliberately narrow: `gles2_compat` program/buffer
  wrappers, `Mesh` (interleaved pos+colour VBOs, lines/tris), `Typer`,
  `WarpPass` (render-to-texture + one post pass), no textures from disk, two
  shaders' worth of GLSL. Reimplementing that surface on D3D12.X is a
  bounded job (rough order: 3–5 weeks) and removes the ANGLE dependency and
  its DLL redistribution question entirely.
- **Option C — SDL3 + its GPU API.** Largest churn (SDL2→SDL3 migration
  across all platforms); only attractive if A and B both look bad.

Recommendation: spike A briefly because its payoff is "zero renderer work";
plan around B as the realistic path. Either way, also switch the console
SDL2 build from the `/U__GDK__` Win32 hack to SDL's official GDK build
(VisualC-GDK / CMake with GDKX) for windowing, input, and audio, and delete
`sdl_gdk_stubs.cpp` for the console target (keep it for Desktop if still
needed).

Exit criteria: a triangle (then the menu) rendering on the dev kit.

### Phase 3 — Console bring-up

With rendering decided, make the full game run on the dev kit:

1. Entry point: rework the `_GAMING_XBOX` half of `xbox_main.cpp` to match
   the Phase 2 outcome (swap chain/present instead of `eglSwapBuffers` if
   Option B; real surface creation if Option A). Fixed 60 Hz vsync;
   resolution from the console display mode (1080p on S-class profiles,
   4K on X — test both).
2. Input: verify pad hot-plug, two-pad 2P, disconnect auto-pause on console
   (logic exists; the SDL GDK backend is the new variable).
3. Audio: verify SDL_mixer over SDL's GDK audio backend; 48 kHz, 32 channels.
4. File I/O: verify `SDL_GetPrefPath()` lands in persistent local storage on
   console and survives reboot; verify relative `audio/` loads from the
   mounted package (working directory inside an `.xvc` — may need
   `SDL_GetBasePath()` prefixing; make a small `asset_path()` helper if so).
5. Performance: profile late-generation worlds (many asteroids + warp pass).
   `WarpPass` does a full-viewport copy per frame when invisible asteroids
   exist — at 4K this is the most likely frame-budget problem; fall back to
   rendering at 1080p→upscale if needed (acceptable for this art style).

Exit criteria: full game loop, 2P, save/load, stable frame rate on dev kit.

### Phase 4 — Certification feature work

All in plain C++ behind small abstractions; testable on dev kit only.

1. **PLM / lifecycle:** verify suspend (`plm_suspend_callback` → save →
   `XSuspendResumeAcknowledge`) completes within the time budget; handle
   resume timer reset (exists: `s_reset_tick`); handle constrained mode
   (Quick Resume) — treat like focus loss. Register for resume callbacks
   too, not just suspend.
2. **User identity:** add minimal `XUserAddAsync` sign-in at boot
   (silent default user), handle sign-out mid-game (pause + return to
   menu). Required by GDK cert even for offline titles.
3. **Saves:** evaluate whether LocalState/PLS persistence passes cert for
   user save data or whether `XGameSave` (Connected Storage) is required.
   If required: add a `SaveStorage` abstraction over the existing
   `savegame.cpp` / `preferences.cpp` / highscore `fopen` calls (they
   already funnel through two path helpers, so this is a small seam), with
   the XGameSave implementation keyed to the signed-in XUser.
4. **TV safe area:** add a safe-area inset (configurable, default 90%) to
   HUD layout in `view/overlay.cpp` / `Typer::resize`, enabled on
   `_GAMING_XBOX`. Use `XDisplay`/title-safe queries if available, else a
   fixed 5% margin per edge.
5. **Cert sweep of behaviours:** no unintended process exit paths (menu
   "quit" should exit cleanly via `XGameUiShowMessageDialog`-free flow —
   plain exit is acceptable for GDK), controller-disconnect messaging,
   no keyboard requirement anywhere (already true), splash screen present.

### Phase 5 — Packaging, CI, store assets

1. Create `xbox/Assets/`: StoreLogo 50×50? — follow current GDK spec sizes
   (config comments list 50×50, 480×480, 150×150, 1920×1080 splash).
2. Fill in `MicrosoftGame.config` identity from Partner Center; align
   `TargetDeviceFamily` (Scarlett) across config + `PackagingLayout.xml`;
   bump `configVersion` if current GDK requires.
3. CI: hosted runners cannot build Scarlett (GDKX is NDA). Either
   (a) a self-hosted Windows runner with GDKX for a `xbox-console.yml`
   compile job, or (b) keep console builds manual and rely on
   `xbox-dev.yml` (Desktop) to catch shared-code breakage — start with (b).
4. Rework `deploy-xbox.yml` before re-enabling: keep the makepkg packaging
   job (run on the self-hosted runner), but verify the submission step —
   StoreBroker may not handle GDK `.xvc`; if not, replace with manual
   Partner Center upload instructions or the current Microsoft-supported
   upload tooling.
5. Test the packaged build (`makepkg` → install via `xbapp install` on dev
   kit) — packaged-mode file paths and launch are different from
   loose-deploy and must be tested explicitly.

### Phase 6 — Cert pass and submission

1. Run the GDK certification test suite locally; fix findings.
2. Full manual test matrix on dev kit: cold boot, Quick Resume, suspend
   during gameplay/menu/game-over, controller swap mid-game, 2P join/leave,
   save corruption recovery (delete/corrupt save), 1080p + 4K displays.
3. Partner Center submission via `deploy-xbox.yml` (or manual upload),
   age ratings, store listing, screenshots.

## 4. Concrete code work-item list

| # | Item | Files | Phase |
|---|------|-------|-------|
| 1 | ✅ Hide keyboard-only rows in help overlay on controller | `glship.cpp` (draw_keymap) | 1 |
| 2 | ✅ Fix "ANGLE bundled with GDK" comment | `xbox/CMakeLists.txt`, `xbox_main.cpp`, `CLAUDE.md` | 1 |
| 3 | Rendering spike + decision (ANGLE-GDKX vs native D3D12.X backend) | new `xbox/` backend or ANGLE fork | 2 |
| 4 | Switch console SDL2 to official GDK build; drop `/U__GDK__` + stubs for console | `xbox/CMakeLists.txt`, `sdl_gdk_stubs.cpp` | 2 |
| 5 | Rework `_GAMING_XBOX` present path per #3 | `xbox_main.cpp` | 3 |
| 6 | ✅ `asset_path()` helper (SDL_GetBasePath prefix on GDK) | `asset_path.h` + 33 audio call sites | 3 |
| 7 | WarpPass 4K performance (profile; optional internal-res scale) | `warp_pass.cpp` | 3 |
| 8 | PLM resume registration + constrained mode | `xbox_main.cpp` | 4 |
| 9 | XUser sign-in + sign-out handling | `xbox_main.cpp`, small `xbox/xbl_user.*` | 4 |
| 10 | `SaveStorage` abstraction + XGameSave impl (if cert requires) | `savegame.cpp`, `preferences.cpp`, `highscore.h`, new `xbox/gdk_storage.*` | 4 |
| 11 | ✅ TV safe-area inset (`Overlay::SAFE_AREA_SCALE`, 90% on `_GAMING_XBOX`) | `view/overlay.h/cpp`, `glgame.cpp` (HUD ortho + minimap viewport), `menu.cpp` | 4 |
| 12 | Store assets + config identity + family alignment | `xbox/Assets/`, `MicrosoftGame.config`, `PackagingLayout.xml` | 5 |
| 13 | Self-hosted console CI job (optional) + deploy workflow rework | `.github/workflows/` | 5 |

## 5. Sequencing and effort (single developer, rough)

- Phase 0: start now; weeks of calendar latency, ~1 day of effort.
- Phase 1: ~1 week, can run during Phase 0 wait.
- Phase 2: 1–2 week spike + decision; if Option B, +3–5 weeks renderer work.
- Phase 3: 2–3 weeks on dev kit.
- Phase 4: 2–3 weeks (XGameSave being the swing item).
- Phase 5: ~1 week.
- Phase 6: 1–2 weeks plus Microsoft cert turnaround.

Realistic total: **3–4 months** elapsed, dominated by ID@Xbox onboarding and
the console rendering backend. Everything before dev-kit arrival (Phases 0–1
and the public-information half of Phase 2) is unblocked today.

## 6. Risk register

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| ANGLE unusable on console | High | Plan around Option B (native D3D12.X behind `gles2_compat`-shaped interface); game's GL surface is small |
| SDL2 GDK console backend gaps | Medium | SDL 2.28+/SDL2-compat track record on Xbox is decent; SDL3 is the escape hatch |
| Cert requires XGameSave | Medium | Storage seam is small (two path helpers); budgeted in Phase 4 |
| StoreBroker can't submit .xvc | Medium | Manual Partner Center upload as fallback; submission is infrequent |
| 4K perf (warp pass) | Low–Medium | Internal-resolution scaling fallback |
| ID@Xbox approval delay | Medium | All desktop-side work proceeds in parallel |
