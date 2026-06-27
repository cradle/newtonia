# Xbox Port — Implementation Plan

Status: planning document. Companion to `xbox/CMakeLists.txt`, `xbox_main.cpp`,
`.github/workflows/xbox.yml`, `xbox/GLON12_SPIKE.md`, and the disabled
`.github/workflows/disabled/deploy-xbox.yml`. GDKX-gated API work (Phases 3–4)
tracks its symbol inventory in `xbox/GDKX_SYMBOLS.md` — the list to diff against
the real SDK the moment GDKX is installed.

## 0. 2026 onboarding/distribution update (changes the Phase 0/5/6 picture)

Microsoft's GDC 2026 / April 2026 overhaul materially loosened the gates this
plan was written against. Confirmed from Microsoft developer docs (see sources
below):

- **The GDK is public.** Docs, samples, and the GDK itself are now publicly
  available (installable via `winget`); no NDA to *read* the docs or build the
  GRDK (Desktop) side. The **GXDK** (Xbox console extension — Scarlett platform,
  D3D12.x, `xb*` tools, `xgameplatform.lib`) is still distributed to enrolled
  developers and **must never reach this public repo's CI** (logs, caches,
  artifacts).
- **Modular onboarding.** Capabilities unlock step-by-step instead of all at
  concept approval. Account creation + verification already grants Partner
  Center, sandboxes, and Xbox services — matching the "Limited GDK capabilities"
  tier on the Partner Center GDK page (Seller ID 94966010).
- **The sandbox is real retail distribution.** Per Microsoft: the development
  sandbox is "real distribution through real Xbox infrastructure, scoped to your
  team… your game gets a real store page in the Xbox app; your testers install
  and update it the same way players will." So a packaged build can be published
  to our sandbox and **installed on a real retail console** (via the Xbox app,
  scoped to the account) *before* full concept approval — a hardware test path
  that doesn't need a loaned dev kit. This supersedes the old "retail Dev Mode
  runs UWP only / hardware needs an ID@Xbox dev kit" assumption in §1 item 5.
- **Foundation Mode (April 15 2026).** Free PlayFab backend (matchmaking,
  cross-saves, leaderboards) for titles committed to shipping on Xbox. Not used
  by v1 (offline only) but available now.

What did **not** change: producing the console binary still needs the GXDK
(Scarlett platform + D3D12.x/GLon12 link), and putting a *retail console into
direct dev-mode F5 deploy* is still NDA-gated. The achievable pre-approval path
is **package → publish to sandbox → install on console**, which is exactly what
the new GLon12 console package build (below) feeds.

Sources: GDC 2026 "What's Changed in Xbox Development"
(developer.microsoft.com/games/articles/2026/03/…); Xbox Game Dev Update
Spring '26 (…/2026/04/…); GDK intro + change-dev-kit-mode docs (learn.microsoft.com).

### Console package build (this branch)

The Scarlett **GLon12 console package** path is now wired end-to-end as
infrastructure (the renderer/present code is the proven `_GAMING_DESKTOP` path;
only the GXDK link + dev-kit run remain):

- `xbox/CMakeLists.txt` — `-DXBOX_SCARLETT=ON` Ninja path: compiles the
  desktop-GL renderer for Scarlett as `_GAMING_DESKTOP + NEWTONIA_GDK_CONSOLE +
  WINAPI_FAMILY_GAMES`, links `xgameplatform.lib`/`xgameruntime.lib` + the Xbox
  GLon12 `opengl32` import lib (`/NODEFAULTLIB`), no `gdi32`/`user32`/`opengl32`.
- `xbox_main.cpp` — mode 2: reuses the SDL_GL present path, layering native-res
  fullscreen + GDK PLM + TV safe-area under `NEWTONIA_GDK_CONSOLE`. The ANGLE
  path (`_GAMING_XBOX`) is preserved only as the abandoned fallback.
- `xbox/build_console_package.ps1` — hand-run configure → build → stage →
  `makepkg` → `.xvc` on a GXDK machine.
- `.github/workflows/disabled/deploy-xbox.yml` — same pipeline for a self-hosted
  `gdkx` runner (replaces the old `exit /b 1` stub), staging the GLon12 DLLs.

Still GXDK-gated (cannot run on this repo's CI / on Linux): a Mesa-for-Xbox
GLon12 build (`opengl32.dll`/`libgallium_wgl.dll` + import lib) and SDL's
official `VisualC-GDK` backend (work-item #4). Both are marked TODO in the
build files.

## 1. Where the port stands today

### Proven (builds in CI, code reviewed)

| Area | State |
|------|-------|
| GDK Desktop build | Green on every PR via the **self-hosted** Windows runner (`xbox.yml`, `runs-on: [self-hosted, windows]`). Plain Ninja + MSVC, **no GDK and no ANGLE**: `cmake -B xbox/build-desktop -S xbox` builds standalone against SDL2/SDL2_mixer (FetchContent) + `opengl32`, statically linked (`/MT`) so the artifact is just `newtonia.exe` with no extra runtime DLLs. (The old hosted Ninja+BWOI-GDK / ANGLE-DLL `xbox-dev.yml` is retired into `.github/workflows/disabled/`.) |
| Renderer (Desktop) — **GLon12-proven** | The Desktop target compiles the desktop **GL 3.3 core** renderer (split on `NEWTONIA_XBOX_CONSOLE`) over SDL's WGL backend (`SDL_GL_CreateContext` / `SDL_GL_SwapWindow`). The full `newtonia.exe` (not just the §2 probe) boots and runs through Mesa's GLon12 — `vendor=Microsoft Corporation renderer=D3D12 (NVIDIA GeForce RTX 5080) version=4.6 (Core Profile) Mesa 24.3.4` (`xbox/GLON12_SPIKE.md`). This is the Microsoft-supported OpenGL-on-console path (Option A), now de-risked end-to-end on a desktop GPU; only the Xbox Game OS feature-level re-confirmation under GDKX remains. |
| Entry point | `xbox_main.cpp` handles both Desktop and console. Desktop creates its GL context with `SDL_GL_CreateContext` / `SDL_GL_SwapWindow`; the `_GAMING_XBOX` console half keeps the manual EGL/ANGLE context (GDKX-gated, untested on hardware). |
| Controller-only operation | Complete. Menu/options fully navigable by pad (`menu.cpp:291-409`); all ship actions mapped (`glship.cpp:325-449`); pause = START, quit-to-menu = BACK, add player 2 = START (`glgame.cpp:1448-1562`); hot-plug + per-player assignment in `state_manager.cpp:37-60`; auto-pause on controller disconnect (`glgame.cpp:447-455`) |
| Help overlay | Switches to controller glyphs automatically (`glship.cpp:646-794`) |
| Rendering abstraction | Desktop takes the **desktop GL 3.3 core** path (`gl_compat.h`, the GLon12-capable route); the console target still takes the GLES2/ANGLE path (`gles2_compat.h`) — same code as Android/iOS/Web — until GDKX lands. Split on `NEWTONIA_XBOX_CONSOLE` |
| Save / prefs / highscore | All via `SDL_GetPrefPath()` + `fopen` — maps to GDK LocalState; no hardcoded paths |
| Audio | Relative `audio/` paths, staged next to exe by CMake post-build; 48 kHz mixer matches Xbox |
| Suspend handling | `focus_lost()` saves progress + pauses (`glgame.cpp:409-416`); Xbox PLM suspend callback wired (`xbox_main.cpp:163-169`) |
| Packaging scaffolding | `MicrosoftGame.config`, `PackagingLayout.xml`, `deploy-xbox.yml` (disabled) with makepkg + StoreBroker steps |

### Untested / known-wrong (the actual port work)

1. **The console *entry-point* specifics run nowhere on hardware yet.** Live CI
   now covers the parts that matter without a dev kit: the self-hosted
   `xbox.yml` builds the Desktop / desktop-GL path, and the hosted
   `xbox-console-smoke.yml` compiles the renderer + shared game code against the
   **Xbox Game OS API partition** (`WINAPI_FAMILY_GAMES`) on every PR, catching
   forbidden-API leakage. That smoke deliberately compiles the **desktop-GL /
   GLon12 renderer** (`_GAMING_DESKTOP` → `DESKTOP_COMPAT_GL`) — the decided
   console path — not the abandoned ANGLE/GLES2 flow. What still runs nowhere:
   the `_GAMING_XBOX` windowing/present/PLM specifics (manual EGL context today;
   GLon12 surface + PLM/XUser later), which stay a compile-time reference until
   GDKX + a dev kit. Nothing runs on console hardware.
2. **The console rendering strategy is now decided, not open — Option A
   (GLon12).** The desktop half of the Phase 2 spike passed: the existing
   GL 3.3 core renderer runs through Mesa's real OpenGL→D3D12 (GLon12) path on a
   desktop GPU (see the Proven table + `xbox/GLON12_SPIKE.md`), so ANGLE is no
   longer the plan. The console target still *links* ANGLE as a leftover
   placeholder, but the path forward is GLon12 + the desktop-GL renderer.
   Outstanding, GDKX-gated: building Mesa's `d3d12` driver for the Xbox cross
   target and re-confirming GLon12's exposed GL version under the **Xbox Game OS
   feature level**. ANGLE-on-console — never officially built — drops to an
   abandoned fallback.
3. **SDL2 console windowing is unvalidated.** SDL's WGL backend is now proven
   on **Desktop** (the GLon12 path uses `SDL_GL_CreateContext` /
   `SDL_GL_SwapWindow` against a stock Win32 window), but the console exposes a
   restricted API surface and needs SDL's official `VisualC-GDK` build (GDKX) —
   work-item #4. The current console path still compiles SDL2 with `/U__GDK__`
   (plain Win32 backend) and assumes `SDL_GetWindowWMInfo` yields a usable HWND;
   that assumption is untested on a dev kit.
4. **The public GDK does not include the Scarlett platform.** The winget
   `Microsoft.Gaming.GDK` package is the GRDK (Desktop only). The
   `Gaming.Xbox.Scarlett.x64` MSBuild platform, D3D12.X, and the console
   toolchain come with GDKX, which is NDA-distributed to ID@Xbox members and
   must not be uploaded to public CI runners. The disabled `deploy-xbox.yml`
   as written (Scarlett configure on a hosted runner) cannot work.
5. **Hardware access — eased by the 2026 sandbox path (§0).** The old
   assumption ("retail Dev Mode runs UWP only; GDK packages need a loaned dev
   kit") is superseded: a packaged build can be **published to the team sandbox
   and installed on a retail console via the Xbox app** before concept approval.
   Producing the package still needs the GXDK; direct dev-mode F5 deploy to a
   retail console remains NDA-gated.
6. **Packaging details incomplete.** ~~`TargetDeviceFamily` mismatch~~ (now
   aligned to Scarlett) and ~~missing `xbox/Assets/`~~ (placeholder art
   generated by `xbox/generate_assets.py`) are resolved; identity/StoreId
   fields are still TODO placeholders pending Partner Center. StoreBroker is
   an appx/MSIX-era tool — whether it can submit GDK `.xvc` packages needs
   verification (console packages are normally uploaded via Partner Center /
   the game package upload flow).
7. ~~**No TV safe-area handling.**~~ Resolved (work-item #11): a configurable
   title-safe inset (`Overlay::SAFE_AREA_SCALE`, 90% on `_GAMING_XBOX`) now
   applies across the HUD ortho + minimap viewport (`view/overlay.*`,
   `glgame.cpp`) and the menu (`menu.cpp`). Still needs visual verification on a
   real TV/dev kit.
8. ~~**Doc bug:** `xbox/CMakeLists.txt` claims ANGLE is bundled with the GDK.~~
   Fixed (work-item #2) across `xbox/CMakeLists.txt`, `xbox_main.cpp`, and
   `CLAUDE.md`.

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

Status (2026-06): Partner Center developer account created (free onboarding
via storedeveloper.microsoft.com); ID@Xbox concept **accepted**, but the
**publishing agreements / NDA are NOT yet signed**. That gate still blocks
everything GXDK-side: GDKX download, the console (Scarlett) binary, a dev kit
loan, and the sandbox-package hardware path all wait on the NDA. Unblocked
*now* (no NDA needed): create the Newtonia title in Partner Center and set the
identity secrets; everything else in this plan that the table at §1 marks as
GDKX-free (Desktop/GLon12 build, console-API compile smoke, the GDKX-free
SaveStorage seam).

1. 🔶 Enrol in ID@Xbox (https://developer.microsoft.com/games) — concept
   accepted; **publishing agreements / NDA still pending** (the GXDK gate).
2. ⏳ Create the Newtonia title in Partner Center → real `Identity/@Name`,
   `@Publisher`, `StoreId`. Stored as GitHub secrets and injected into
   `MicrosoftGame.config`'s `__FILL_*__` tokens at deploy time — nothing
   identity-related is committed (checklist: `xbox/PARTNER_CENTER_VALUES.md`).
3. ⛔ Download GDKX + request a dev kit loan — **blocked on the NDA (item 1)**;
   GDKX is distributed only to enrolled developers under the signed agreements.
   Request the kit the moment the NDA clears (longest lead time). The Phase 2
   rendering spike's GDKX-free half is already done (`xbox/GLON12_SPIKE.md`), so
   nothing else waits on this.
4. Decide publisher display name, age ratings (IARC questionnaire), pricing.
5. ⏳ Stand up a **private repo** for GDKX-touching console work (NDA) — the
   public repo keeps the game + all non-NDA work. Use a private mirror with
   `upstream` tracking, not a submodule (the Xbox code is woven into shared
   files). Recipe + public/private split: `xbox/PRIVATE_REPO.md`.

Exit criteria: GDKX installable on a dev machine; dev kit on the desk.

### Phase 1 — Harden GDK Desktop (no new hardware needed, start now)

Status (2026-06): the GDK Desktop CI build is **green and reliable** — it
builds via Ninja + MSVC with a BWOI (lessmsi-extracted) GDK on
windows-latest, sidestepping the GDK MSBuild platform's VCTargetsPath probe
(see Phase 5). The artifact runs (controller hot-plug bug found and fixed,
issue #287; pad/game-over disconnect handling fixed). Remaining: complete the
manual test-pass checklist.

1. Manual test pass of the CI artifact on a Windows machine: boot, menu,
   options persistence, 1P/2P with pads, pause/focus loss, save/resume,
   fullscreen toggle, window resize, quit. Log via `newtonia.log`.
   **Checklist: `xbox/DESKTOP_TEST_PASS.md`.**
2. Fix whatever that pass shakes out (likely candidates: pbuffer blit
   performance at large window sizes — `glReadPixels` + `StretchDIBits` is
   CPU-bound; acceptable for a dev vehicle, document as Desktop-only).
3. ✅ Polish: hide keyboard-only cheat rows (skip level, time controls, debug
   grid, fullscreen) from the help overlay when `last_input_was_controller`
   — they're unreachable and confusing on pad. (Done; `glship.cpp`.)
4. Verify suspend→resume→save integrity by minimising/restoring mid-game.

Exit criteria: Desktop build is a trustworthy reference for "the game logic
and GLES2 renderer are correct on Windows/ANGLE".

### Phase 2 — Console rendering decision spike

Decide how our GL calls reach the console GPU. Timebox ~1–2 weeks of
investigation before committing.

**What splits cleanly across the GDKX gate.** The dominant risk in this phase
is "does GLon12 (Mesa OpenGL-on-D3D12) expose a GL version/feature set that
covers our full GL 3.3 core renderer?" That question is answerable **today, with
no GDKX and no dev kit**: Mesa ships a public *desktop* GLon12 build
(`opengl32.dll` + `libgallium_wgl.dll` + `dxil.dll`) that translates desktop
OpenGL to desktop D3D12. Dropping those DLLs next to a normal Windows build and
running our renderer through them exercises the same Gallium d3d12 driver and
the same GL-feature surface the console build will use — only the D3D12 backend
(desktop DXGI vs `D3D12.x` Game OS) and the SDL windowing layer differ.

- **GDKX-free (do now):**
  - Confirm GLon12 exposes ≥ GL 3.3 core + GLSL 330 and resolves every GL entry
    point our renderer calls (`COMPAT_GL_FNS` in `gles2_compat.cpp` + core 1.x).
  - Confirm the renderer's GLSL 330 program compiles/links/draws through GLon12,
    and that a frame actually rasterises (triangle → menu).
  - Prototype the **SDL2 WGL path** the console will use (`SDL_GL_CreateContext`
    + `SDL_GL_SwapWindow` against the desktop-GL renderer) instead of the current
    ANGLE/EGL-pbuffer Desktop path — work-item #4 merged in.
  - Tooling for the above: **`xbox/glon12/`** (`glon12_probe.cpp`, CMake, local
    run scripts) and **`xbox/GLON12_SPIKE.md`** (results log + how to run).
- **GDKX-required (deferred until dev kit + GDKX):**
  - Build Mesa with `-Dgallium-drivers=d3d12` for the **Xbox cross target** and
    ship `opengl32.dll` + `libgallium_wgl.dll` next to the exe (GLon12-for-Xbox
    needs GDK support + a Microsoft agreement; does not work with the PC GDK).
  - Confirm the **Xbox Game OS feature level** doesn't lower GLon12's exposed GL
    version below what the desktop spike measured.
  - Run the renderer on the dev kit (the real exit criterion below).

- **Option A — SDL2 WGL + GLon12 (Mesa OpenGL-on-D3D12) + the existing
  desktop-GL path.** The Microsoft-blessed route for OpenGL on the console, and
  proven in shipped titles (e.g. *Steel Assault*). How the stack fits together:
    - **Runtime path:** `our GL calls → SDL2 WGL backend → opengl32.dll +
      libgallium_wgl.dll (Mesa GLon12) → D3D12.x → GPU`. This replaces ANGLE's
      `GLES2 → EGL → D3D11` stack entirely.
    - **SDL side (already SDL2):** SDL's "build with OpenGL on Xbox" change is
      on the SDL2 (`VisualC-GDK`) branch — exactly the SDL we use. It doesn't
      translate anything itself; it makes the WGL video backend work on the
      Game OS by remapping `GetDC()` (treats the `HWND` as the `HDC`), loading
      the `wgl*` entry points via `SDL_LoadFunction()`, and supplying a
      `PIXELFORMATDESCRIPTOR` the Xbox headers omit. Apps then use stock
      `SDL_GL_CreateContext()` / `SDL_GL_SwapWindow()`. (Note: the SDL3 GLon12
      path has a tracked context-creation regression, libsdl-org/SDL#11247 —
      not our concern on SDL2.)
    - **GLon12 side:** build Mesa with `-Dgallium-drivers=d3d12` for the Xbox
      cross target and ship `opengl32.dll` + `libgallium_wgl.dll` next to the
      exe (a redistribution/build chore, but supported). GLon12-for-Xbox needs
      GDK support + a Microsoft agreement and does **not** work with the
      PC-only GDK, so it's a GDKX/console-only path under the same NDA gate.
    - **Our renderer side:** GLon12 exposes **desktop OpenGL**, not GLES — but
      we already have a working desktop **GL 3.3 core** renderer (`gl_compat.h`),
      so on console we'd compile that path instead of the GLES2 one, and drop
      the manual EGL/ANGLE context in `xbox_main.cpp`'s `_GAMING_XBOX` half for
      `SDL_GL_CreateContext` + `SDL_GL_SwapWindow`. Potentially the
      lowest-effort route of all.
    - **The spike's job:** confirm GLon12's exposed GL version covers our full
      GL 3.3 core feature set. The bulk of this is GDKX-free — Mesa's desktop
      GLon12 build runs the same Gallium d3d12 driver on a normal Windows box
      (see `xbox/glon12/` + `xbox/GLON12_SPIKE.md`). Only re-confirming the
      version under the **Xbox Game OS feature level**, building Mesa for the
      Xbox cross target, and running on the dev kit need GDKX. This option merges
      with work-item #4 (move the console build to SDL's official `VisualC-GDK`
      build) — they are one task.
- **Option B — compile ANGLE against GDKX D3D11.X/D3D12.** Keeps the GLES2
  renderer untouched. Unknown effort; **no official ANGLE build for Xbox
  consoles**, and ANGLE's D3D backends assume desktop DXGI swap chains. Lower
  priority than A now that GLon12 is the supported translation path; investigate
  only if GLon12 can't cover our GL feature set, and abandon quickly if
  swap-chain/device creation can't be adapted.
- **Option C — native D3D12.X backend behind the existing abstraction.**
  The game's GPU usage is deliberately narrow: `gles2_compat` program/buffer
  wrappers, `Mesh` (interleaved pos+colour VBOs, lines/tris), `Typer`,
  `WarpPass` (render-to-texture + one post pass), no textures from disk, two
  shaders' worth of GLSL. Reimplementing that surface on D3D12.X is a
  bounded job (rough order: 3–5 weeks) and removes the ANGLE/GLon12 dependency
  and its DLL redistribution question entirely.
- **Option D — SDL3 + its GPU API.** Largest churn (SDL2→SDL3 migration
  across all platforms); only attractive if A–C all look bad.

Recommendation: spike A first (GLon12) — it is the supported path and may reuse
the existing desktop-GL renderer with little code; plan around C as the safe
fallback. **Update (2026-06): the GDKX-free half of the A spike passed** — the
existing GL 3.3 core renderer runs through GLon12's real OpenGL→D3D12 path on a
desktop GPU (`xbox/GLON12_SPIKE.md`), so A is reusing the desktop-GL renderer as
hoped; only the console feature-level confirmation under GDKX is outstanding. B (ANGLE-fork) drops down the list. Either way, also switch the console
SDL2 build from the `/U__GDK__` Win32 hack to SDL's official GDK build
(VisualC-GDK / CMake with GDKX) for windowing, input, and audio, and delete
`sdl_gdk_stubs.cpp` for the console target (keep it for Desktop if still
needed).

Exit criteria: a triangle (then the menu) rendering on the dev kit. The
GDKX-free milestone (same triangle + GL-feature probe through Mesa's desktop
GLon12 on a Windows box / hosted CI) is the de-risking gate before that.

### Phase 3 — Console bring-up

With rendering decided, make the full game run on the dev kit.
**Checklist: `xbox/CONSOLE_BRINGUP.md`** (mirrors the Desktop test pass).

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
   too, not just suspend. **Verify the API itself:** `XSuspendResume.h` /
   `XSuspendResumeRegisterForSuspend` do not appear in public GDK docs, and
   SDL's GDK backend uses `RegisterAppStateChangeNotification`
   (`appnotify.h`) instead — the entry point's PLM block was written
   without a compiler and must be checked against the real GDKX headers. (The
   `_GAMING_XBOX` PLM block is no longer exercised by any CI — the console
   smoke now compiles the `_GAMING_DESKTOP` renderer path; the GDK-API stubs in
   `xbox/smoke_stubs/`, which mirror our assumptions rather than the real API,
   remain only as a reference for this GDKX-gated work.)
2. **User identity:** add minimal `XUserAddAsync` sign-in at boot
   (silent default user), handle sign-out mid-game (pause + return to
   menu). Required by GDK cert even for offline titles.
3. **Saves:** evaluate whether LocalState/PLS persistence passes cert for
   user save data or whether a cloud-roaming store is required. The
   `SaveStorage` seam is now in place (`save_storage.h/.cpp`): all persisted
   paths resolve through `SaveStorage::path_for(Category, file)`, with Roaming
   (savegame.dat, highscore.dat) split from Local (preferences.ini). Off-console
   both resolve to `SDL_GetPrefPath()` (unchanged). The GDK roaming impl —
   `XGameSaveFiles` keyed to the signed-in XUser (also the XR-052 / Play Anywhere
   requirement, Phase 7) — is the `NEWTONIA_XGAMESAVE`-gated block in
   `save_storage.cpp`, waiting on GDKX + sign-in (#9). Because XGameSaveFiles
   returns a real folder, only path resolution changes; the save/load code is
   untouched.
4. **TV safe area:** add a safe-area inset (configurable, default 90%) to
   HUD layout in `view/overlay.cpp` / `Typer::resize`, enabled on
   `_GAMING_XBOX`. Use `XDisplay`/title-safe queries if available, else a
   fixed 5% margin per edge.
5. **Cert sweep of behaviours:** no unintended process exit paths (menu
   "quit" should exit cleanly via `XGameUiShowMessageDialog`-free flow —
   plain exit is acceptable for GDK), controller-disconnect messaging,
   no keyboard requirement anywhere (already true), splash screen present.

### Phase 5 — Packaging, CI, store assets

1. ✅ Create `xbox/Assets/` — placeholder art generated by
   `xbox/generate_assets.py` (StoreLogo 100×100, 150/480 tiles, 1920×1080
   splash). Replace with real art before submission.
2. 🔶 `MicrosoftGame.config`: `TargetDeviceFamily` aligned to Scarlett across
   config + `PackagingLayout.xml` ✅; identity kept out of source as
   `__FILL_*__` tokens injected from GitHub secrets at deploy time
   (checklist `xbox/PARTNER_CENTER_VALUES.md`) ✅ — set the secrets once the
   Partner Center title exists.
3. CI: there are four tiers of "Xbox build in CI", confirmed against
   Microsoft's docs (public GDK README, BWOI docs, vcpkg Xbox triplets):
   - **GDK Desktop on hosted runners** — ✅ works (`xbox-dev.yml`). NOTE: the
     original winget + `-A Gaming.Desktop.x64` (MSBuild platform) approach
     broke on 2026 hosted images (windows-latest → VS2026 breaks the GDK
     VS2022 platform; windows-2022 has no winget and the GDK installer's
     GamingServices Appx step / `msiexec /a` both hang). Now builds with
     **Ninja + MSVC + a BWOI lessmsi-extracted GDK** (headers wired in,
     `_GAMING_DESKTOP` defined), which avoids the MSBuild platform and its
     unsolvable VCTargetsPath probe entirely. Runner-agnostic → runs on
     windows-latest. This Ninja+BWOI structure is the basis for the console
     (Scarlett) target.
   - **Console API-surface compile smoke on hosted runners** — active today
     with zero NDA material (`xbox-console-smoke.yml`): plain MSVC +
     Windows SDK ≥ 22000 compiling all sources with
     `/DWINAPI_FAMILY=WINAPI_FAMILY_GAMES /D_GAMING_DESKTOP`. The Xbox Game OS
     partition excludes GDI/D3D11/desktop-GL/WGL APIs, so this catches
     forbidden-API usage in the **desktop-GL/GLon12 renderer** (the decided
     console path) + shared game code on every PR. It defines `_GAMING_DESKTOP`
     (→ `DESKTOP_COMPAT_GL`), **not** `_GAMING_XBOX`, so the abandoned
     ANGLE/GLES2 flow and its EGL/PLM code go uncompiled; the GDK header stubs
     (`xbox/smoke_stubs/`) are therefore unused while that path is dormant.
     No link step.
   - **Full `Gaming.Xbox.Scarlett.x64` build** — requires the NDA GDKX
     (ID@Xbox); the Scarlett MSBuild platform, D3D12.X, and console libs
     are not in the public GDK, so stock hosted runners cannot do it.
     Microsoft's "Build WithOut Installing" (BWOI) flow is designed for CI
     agents (extracted GDK/GXDK + env vars). Recommended: self-hosted
     Windows runner with GDKX. A hosted runner pulling extracted GDKX from
     private storage is mechanically possible but needs an NDA-terms check
     first — and this repo is public, so GDKX bytes must never reach logs,
     caches, or artifacts.
   - **Packaging/submission** — same GDKX constraint as above.
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

### Phase 7 — Xbox Play Anywhere (post-launch goal)

Buy-once, play-on-both: a single purchase grants entitlement on both Xbox
console and Windows PC (via the Xbox app / Microsoft Store), with cloud-roaming
saves so progress carries across devices.

Requirements (ref: https://developer.microsoft.com/en-us/games/resources/xbox-play-anywhere/):

1. Publish both the **Windows Desktop** and **Xbox Console** SKUs under the same
   Partner Center product (same Title ID). Cross-entitlement is automatic once
   both are published.
2. Use `XGameSaveFiles` (or another XR-052-compliant save mechanism) with the
   same Title ID and XUID so saves roam between devices. This is the same
   roaming work as item 10 — the `SaveStorage` seam (`save_storage.h/.cpp`)
   already routes the Roaming category (savegame.dat, highscore.dat) through a
   single call; only its `NEWTONIA_XGAMESAVE`-gated `XGameSaveFiles` body needs
   filling in (GDKX + sign-in). Preferences stay Local, exactly as on desktop.
3. Verify the shared save format works across both builds (same `savegame.cpp`
   binary format, magic "NWTN" v10 — already shared).
4. Partner Center configuration: enable Xbox Play Anywhere for the product.

Newtonia is well-positioned: both targets share the same codebase, renderer,
and save format, and the Roaming/Local storage seam is already in place. The
only code gap is filling in the `XGameSaveFiles` body behind `SaveStorage`
(item 10, Phase 4). The rest is Partner Center configuration.

## 4. Concrete code work-item list

| # | Item | Files | Phase |
|---|------|-------|-------|
| 1 | ✅ Hide keyboard-only rows in help overlay on controller | `glship.cpp` (draw_keymap) | 1 |
| 2 | ✅ Fix "ANGLE bundled with GDK" comment | `xbox/CMakeLists.txt`, `xbox_main.cpp`, `CLAUDE.md` | 1 |
| 3 | Rendering spike + decision (GLon12 + desktop-GL path vs ANGLE-GDKX vs native D3D12.X backend) | new `xbox/` backend, GLon12 link, or ANGLE fork | 2 |
| 3a | ✅ GDKX-free GLon12 desktop spike **done**: Newtonia's GL 3.3 core renderer runs through GLon12's real OpenGL→D3D12 path on desktop GPU (`D3D12 (NVIDIA RTX 5080)`, 29/29 entry points, GLSL ≥330, triangle). Hosted-CI confirmation via llvmpipe is recorded in `xbox/GLON12_SPIKE.md`; that harness (`windows-glon12-spike.yml`) is now retired/disabled since the result is conclusive. Only the Xbox Game OS feature level under GDKX remains | `xbox/glon12/`, `xbox/GLON12_SPIKE.md` | 2 |
| 4 | Switch console SDL2 to official `VisualC-GDK` build (enables the WGL+GLon12 path; merges with #3); drop `/U__GDK__` + stubs for console | `xbox/CMakeLists.txt`, `sdl_gdk_stubs.cpp` | 2 |
| 4a | ✅ **Desktop** target moved to the SDL WGL + desktop-GL-core renderer (the GLon12-capable path), GDKX-free: `_GAMING_DESKTOP` now compiles the desktop GL shim (split `NEWTONIA_NO_GLUT` from `DESKTOP_COMPAT_GL`) and creates its context with `SDL_GL_CreateContext`/`SDL_GL_SwapWindow`; ANGLE/EGL + pbuffer→GDI blit dropped for Desktop (kept for console). Links only SDL2 + `opengl32`; builds without the GDK (`cmake -S xbox`). **Full-game GLon12 confirmed**: the actual `newtonia.exe` (not just the probe) boots through GLon12/D3D12 — `GL context: vendor=Microsoft Corporation renderer=D3D12 (NVIDIA GeForce RTX 5080) version=4.6 (Core Profile) Mesa 24.3.4` (see `xbox/GLON12_SPIKE.md`). The self-hosted `xbox.yml` compiles this target on every PR as a regression gate. Console (#4/#5) stays ANGLE until GDKX | `gl_compat.h`, `gles2_compat.h/.cpp`, `xbox_main.cpp`, `xbox/CMakeLists.txt` | 2 |
| 5 | ✅ Console present path = the GLon12 decision: added mode 2 (`_GAMING_DESKTOP` + `NEWTONIA_GDK_CONSOLE`) reusing the SDL_GL present path with native-res fullscreen + PLM + safe-area layered on; ANGLE `_GAMING_XBOX` kept as abandoned fallback. GXDK link still gated | `xbox_main.cpp`, `view/overlay.cpp` | 3 |
| 5b | 🔶 Scarlett **GLon12 console package** build wired: `-DXBOX_SCARLETT=ON` CMake path (xgameplatform + GLon12 link, Game OS partition), `xbox/build_console_package.ps1`, and a real configure→build→makepkg `deploy-xbox.yml`. GXDK-gated bits (Mesa-for-Xbox GLon12 redist, SDL VisualC-GDK) remain TODO | `xbox/CMakeLists.txt`, `xbox/build_console_package.ps1`, `xbox/PackagingLayout.xml`, `deploy-xbox.yml` | 5 |
| 6 | ✅ `asset_path()` helper (SDL_GetBasePath prefix on GDK) | `asset_path.h` + 33 audio call sites | 3 |
| 7 | WarpPass 4K performance (profile; optional internal-res scale) | `warp_pass.cpp` | 3 |
| 8 | PLM resume registration + constrained mode | `xbox_main.cpp` | 4 |
| 9 | XUser sign-in + sign-out handling | `xbox_main.cpp`, small `xbox/xbl_user.*` | 4 |
| 10 | 🔶 `SaveStorage` seam **done (GDKX-free)**: `save_storage.h/.cpp` resolves every persisted path through one `SaveStorage::path_for(Category, file)` call, splitting Roaming (savegame.dat, highscore.dat) from Local (preferences.ini) to match `steam/CLOUD.md`; `savegame.cpp`/`highscore.cpp`/`preferences.cpp` refactored onto it with identical on-disk behaviour off-console. The GDK `XGameSaveFiles` roaming impl is a stubbed, `NEWTONIA_XGAMESAVE`-gated block in `save_storage.cpp` (compiled nowhere yet) — drops in once GDKX + sign-in (#9) land; the seam is path-only since XGameSaveFiles returns a folder. | `save_storage.h/.cpp`, `savegame.cpp`, `highscore.cpp`, `preferences.cpp` | 4 |
| 11 | ✅ TV safe-area inset (`Overlay::SAFE_AREA_SCALE`, 90% on `_GAMING_XBOX`) | `view/overlay.h/cpp`, `glgame.cpp` (HUD ortho + minimap viewport), `menu.cpp` | 4 |
| 12 | 🔶 Store assets + config identity + family alignment — placeholder art generated (`xbox/generate_assets.py` → `xbox/Assets/`), `TargetDeviceFamily` aligned to Scarlett, and identity kept out of source as `__FILL_*__` tokens injected from GitHub secrets at deploy time (checklist `xbox/PARTNER_CENTER_VALUES.md`; deploy workflow checks secrets + post-substitution). Set secrets once Partner Center title exists; real art still TODO | `xbox/Assets/`, `MicrosoftGame.config`, `PackagingLayout.xml`, `xbox/PARTNER_CENTER_VALUES.md`, `deploy-xbox.yml` | 5 |
| 13 | Self-hosted console CI job (needs GDKX) + deploy workflow rework | `.github/workflows/` | 5 |
| 14 | ✅ Console API-surface compile smoke on hosted runners (`WINAPI_FAMILY_GAMES` + `_GAMING_DESKTOP`, compile-only — guards the desktop-GL/GLon12 renderer + shared code against the Game OS partition; ANGLE/GLES2 flow no longer compiled) | `.github/workflows/xbox-console-smoke.yml` | 5 |
| 15 | ⏳ **Xbox Play Anywhere** — publish both Windows Desktop + Xbox Console SKUs under the same Title ID; migrate saves to `XGameSaveFiles` (XR-052) so progress roams across devices; enable Play Anywhere in Partner Center. Code dep: item 10 (`XGameSave`). No new renderer/game code needed | Partner Center config, `savegame.cpp`, `xbox/gdk_storage.*` | 7 |

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
| ANGLE unusable on console | High | Expected — no official ANGLE Xbox build. Primary path is GLon12 (Mesa OpenGL-on-D3D12, officially GDK-supported) reusing the existing desktop-GL renderer; fallback is a native D3D12.X backend behind the `gles2_compat`-shaped interface (game's GL surface is small) |
| GLon12 GL version/feature gaps under Game OS | Medium | Spike its supported GL version vs our GL 3.3 core usage early; native D3D12.X backend is the fallback |
| SDL2 GDK console backend gaps | Medium | SDL 2.28+/SDL2-compat track record on Xbox is decent; SDL3 is the escape hatch |
| Cert requires XGameSave | Medium | Storage seam is small (two path helpers); budgeted in Phase 4 |
| StoreBroker can't submit .xvc | Medium | Manual Partner Center upload as fallback; submission is infrequent |
| 4K perf (warp pass) | Low–Medium | Internal-resolution scaling fallback |
| ID@Xbox approval delay | Medium | All desktop-side work proceeds in parallel |
