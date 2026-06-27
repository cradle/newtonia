# GDKX symbols to verify — the "open it the moment GDKX lands" list

A running inventory of every GDKX / console-only API symbol Newtonia **uses or
assumes today**, so that when GDKX is installed we can diff our assumptions
against the real headers in one pass instead of discovering them one compile
error at a time.

**Why this exists:** the NDA GDKX headers are not in this (public) repo and not
in the model's knowledge. The `_GAMING_XBOX` code paths were written *without a
compiler against the real SDK* — they compile in CI only against the
**assumption mirrors** in `xbox/smoke_stubs/` (see `xbox-console-smoke.yml`,
which actually builds the `_GAMING_DESKTOP` renderer, so even those stubs go
uncompiled right now). Every signature below marked **ASSUMED** comes from our
own stub or from how we call it — treat it as a hypothesis to confirm, not fact.

**Workflow when GDKX arrives:** run me where I can `Read`/`Grep` the GDKX
include tree (local Claude Code or a self-hosted runner — never commit GDKX
bytes to this public repo). For each row: locate the real header, compare the
signature/enum/return type, fix the call site, delete the corresponding stub.
Keep NDA header contents out of commits, PR bodies, and logs.

Status legend: ❓ unverified assumption · 🔧 needs new code (not written yet) ·
⚠️ likely wrong / flagged in PORT_PLAN · ✅ confirmed against real GDKX.

---

## 1. Process Lifetime Management (PLM) — suspend/resume

The single biggest "known-wrong" area (PORT_PLAN Phase 4.1). Our entire PLM
block may be using the wrong API: **SDL's GDK backend uses
`RegisterAppStateChangeNotification` (`appnotify.h`), not `XSuspendResume*`**,
and `XSuspendResume.h` does not appear in public GDK docs. First task is to
decide which API actually exists, then rewrite `xbox_main.cpp`'s PLM block.

Call sites: `xbox_main.cpp:54-55` (includes), `:133-143` (queue/token/callback),
`:383-389` (create + register), `:572-575` (unregister + close).
Assumption mirror: `xbox/smoke_stubs/XSuspendResume.h`, `xbox/smoke_stubs/XTaskQueue.h`.

| Symbol | Header (assumed) | Our assumed signature / use | Status |
|--------|------------------|------------------------------|--------|
| `XTaskQueueHandle` | `XTaskQueue.h` | opaque `XTaskQueueObject*` | ❓ |
| `XTaskQueueDispatchMode` | `XTaskQueue.h` | enum: `Manual`, `ThreadPool`, `SerializedThreadPool`, `Immediate` | ❓ |
| `XTaskQueueRegistrationToken` | `XTaskQueue.h` | struct `{ uint64_t token; }` | ❓ |
| `XTaskQueueCreate` | `XTaskQueue.h` | `HRESULT(mode work, mode completion, XTaskQueueHandle*)` — we pass `(ThreadPool, Manual, &q)` | ❓ |
| `XTaskQueueCloseHandle` | `XTaskQueue.h` | `void(XTaskQueueHandle)` | ❓ |
| `XSuspendResumeAcknowledgmentId` | `XSuspendResume.h` | `unsigned int` | ❓ |
| `XSuspendResumeCallback` | `XSuspendResume.h` | `void CALLBACK(void* ctx, ackId)` | ❓ |
| `XSuspendResumeRegisterForSuspend` | `XSuspendResume.h` | `HRESULT(queue, ctx, callback*, token*)` | ⚠️ may not exist — see appnotify below |
| `XSuspendResumeUnregisterForSuspend` | `XSuspendResume.h` | `void(token*)` | ⚠️ |
| `XSuspendResumeAcknowledge` | `XSuspendResume.h` | `void(ackId)` | ⚠️ |
| `RegisterAppStateChangeNotification` | `appnotify.h` | **alternative** SDL uses — likely the real API; confirm signature + that it covers suspend/resume/constrained | 🔧⚠️ |
| `UnregisterAppStateChangeNotification` | `appnotify.h` | pair of the above | 🔧⚠️ |

Also outstanding (PORT_PLAN 4.1 / work-item #8): **register for *resume* too**
(not just suspend) and handle **constrained mode** (Quick Resume) as focus loss.

## 2. GDK runtime init — likely missing entirely

The GDK normally requires the runtime be initialized before any `X*` call and
uninitialized at shutdown. We **do not call these anywhere** — a probable gap on
console (the Desktop path doesn't need them, which is why CI hasn't caught it).

| Symbol | Header (assumed) | Status |
|--------|------------------|--------|
| `XGameRuntimeInitialize` | `XGameRuntime.h` / `XGameRuntimeInitialization.h` | 🔧 not present; confirm it's required for our minimal title and where to call it (before `SDL_Init`?) |
| `XGameRuntimeUninitialize` | same | 🔧 not present |
| `XGameRuntimeFeature` / `XGameRuntimeIsFeatureAvailable` | `XGameRuntimeFeature.h` | 🔧 only if we gate optional features |

## 3. User identity (Phase 4.2, work-item #9)

Minimal silent sign-in at boot; handle sign-out mid-game. Required by GDK cert
even for an offline title. No code yet — planned new file `xbox/xbl_user.*`.

| Symbol | Header (assumed) | Intended use | Status |
|--------|------------------|--------------|--------|
| `XUserHandle` | `XUser.h` | default user handle | 🔧 (only forward-declared today in `sdl_gdk_stubs.cpp:24-25`) |
| `XUserAddAsync` | `XUser.h` | silent default-user sign-in at boot | 🔧 |
| `XUserAddOptions` | `XUser.h` | `AddDefaultUserSilently` (or equivalent) | 🔧 |
| `XUserAddResult` | `XUser.h` | completion result → `XUserHandle` | 🔧 |
| `XUserCloseHandle` | `XUser.h` | release at shutdown | 🔧 |
| `XUserRegisterForChangeEvent` | `XUser.h` | detect sign-out → pause/return to menu | 🔧 |
| `XAsyncBlock` | `XAsync.h` | async plumbing for `XUserAddAsync` (ties to the task queue from §1) | 🔧 |
| `XAsyncGetStatus` / completion routine | `XAsync.h` | drive the async result | 🔧 |

## 4. Save storage — XGameSaveFiles (Phase 4.3, work-item #10; also Phase 7)

**Seam already built (GDKX-free):** all persisted paths now resolve through
`SaveStorage::path_for(Category, file)` in `save_storage.h/.cpp`, splitting
Roaming (savegame.dat, highscore.dat) from Local (preferences.ini). Off-console
both resolve to `SDL_GetPrefPath()` — unchanged behaviour. The GDK roaming
backend is a `NEWTONIA_XGAMESAVE`-gated block in `save_storage.cpp`, compiled
nowhere until GDKX lands.

The intended API is **`XGameSaveFiles`** (the folder/filesystem API), **not** the
lower-level `XGameSave` blob-container API: `XGameSaveFiles` hands back a real
folder that the platform roams across devices (the XR-052 / Play Anywhere
requirement), so we just `fopen` into it and the existing streaming I/O in
`savegame.cpp`/`highscore.cpp` is untouched. Keyed to the signed-in `XUser`
(§3, work-item #9).

| Symbol | Header (assumed) | Our intended use | Status |
|--------|------------------|------------------|--------|
| `XGameSaveFilesGetFolderWithUiAsync` | `XGameSaveFiles.h` | start resolving the roaming folder for the signed-in `XUserHandle` | 🔧 |
| `XGameSaveFilesGetFolderWithUiResult` | `XGameSaveFiles.h` | read the resolved folder path on async completion | 🔧 |
| `XGameSaveFilesGetFolderWithUiResultSize` | `XGameSaveFiles.h` | size the buffer for the result (if the API is two-call) | 🔧 |
| `XAsyncBlock` | `XAsync.h` | async plumbing (shares the §3 task queue) | 🔧 |

Open questions for the seam's `xgamesave_folder()`: (a) is the folder lookup
one-call or size-then-read; (b) does it need the title's SCID/config name;
(c) confirm writes into the folder roam without an explicit commit.

First decision (before any of the above): **does LocalState survive a full
console reboot and pass cert?** (`CONSOLE_BRINGUP.md` §E). If LocalState passes
cert *and* roams for Play Anywhere, the Roaming backend may not be needed — but
Play Anywhere (Phase 7) wants cloud-roaming saves regardless, so the
`XGameSaveFiles` body is the likely endgame either way.

## 5. TV safe area / display (Phase 4.4, work-item #11 — logic done, API optional)

`Overlay::SAFE_AREA_SCALE` (90% on `_GAMING_XBOX`) is already implemented with a
fixed inset. Querying the real title-safe region is *optional* polish.

| Symbol | Header (assumed) | Status |
|--------|------------------|--------|
| `XDisplayTryGetPreferredDisplayModes` (or similar) | `XDisplay.h` | 🔧 optional — confirm what title-safe/overscan query actually exists; else keep the fixed 5% inset |

## 6. SDL ↔ GDK glue — delete when moving to the official `VisualC-GDK` SDL build

Today the console links SDL2 built with `/U__GDK__` (plain Win32 backend) plus
our own no-op `_REAL` stubs. PORT_PLAN work-item #4 / Phase 2 switches the
console to SDL's official `VisualC-GDK` build, at which point **these stubs get
deleted for console** and SDL provides the real ones.

Call sites: `sdl_gdk_stubs.cpp` (all four), `xbox_main.cpp:63` (`GDK_DispatchTaskQueue`).

| Symbol | Provided by (target) | Status |
|--------|----------------------|--------|
| `SDL_GDKGetTaskQueue_REAL` | SDL `VisualC-GDK` | ❓ delete our stub; use SDL's queue for §1/§3 instead of `XTaskQueueCreate`? (decide queue ownership) |
| `SDL_GDKRunApp_REAL` | SDL `VisualC-GDK` | ❓ we bypass via `SDL_MAIN_HANDLED` — confirm that's still valid on console |
| `SDL_GDKSuspendComplete_REAL` | SDL `VisualC-GDK` | ❓ may interact with §1 PLM ack |
| `SDL_GDKGetDefaultUser_REAL` | SDL `VisualC-GDK` | ❓ could replace `XUserAddAsync` for the default user (§3) |
| `GDK_DispatchTaskQueue` | SDL `VisualC-GDK` (`WIN_PumpEvents`) | ❓ our no-op assumes events come elsewhere; the real SDL build pumps the GDK queue here |

**Windowing assumption to verify on hardware** (PORT_PLAN known-wrong #3):
`xbox_main.cpp:261-264` calls `SDL_GetWindowWMInfo` and assumes
`wm.info.win.window` is a usable `HWND` for `eglCreateWindowSurface`. Untested on
a dev kit; the official GDK SDL build is the variable.

## 7. Rendering backend — GLon12 vs ANGLE (Phase 2, GDKX-gated)

Decided path is **GLon12** (Mesa OpenGL-on-D3D12) + the existing desktop-GL
renderer; ANGLE is the abandoned fallback. The GDKX-free desktop half is proven
(`GLON12_SPIKE.md`). What remains is GDKX-gated and is about *build/redistribution*
+ feature-level confirmation, not individual API symbols:

- **GLon12 (intended):** build Mesa `-Dgallium-drivers=d3d12` for the **Xbox
  cross target**; ship `opengl32.dll` + `libgallium_wgl.dll` (+ `dxil.dll`) next
  to the exe. Confirm the **Xbox Game OS feature level** still exposes ≥ GL 3.3
  core / GLSL 330 (desktop measured 4.6). No new game-side symbols — the
  renderer is unchanged; the `_GAMING_XBOX` present path becomes
  `SDL_GL_CreateContext`/`SDL_GL_SwapWindow` (work-item #5), matching Desktop.
- **D3D12.X** types (`ID3D12Device`, swap chain via `IDXG* `/ Game OS present) —
  only relevant if we fall back to **Option C** (native D3D12.X backend). Not
  needed if GLon12 works.
- **ANGLE (abandoned fallback only):** `eglGetDisplay`, `eglInitialize`,
  `eglChooseConfig`, `eglCreateWindowSurface`, `eglCreateContext`,
  `eglMakeCurrent`, `eglSwapBuffers`, `eglSwapInterval`, `eglTerminate`,
  `eglDestroy*` — all already coded in `xbox_main.cpp:266-563`, headers
  `<EGL/egl.h>` + ANGLE libs via `ANGLE_INCLUDE_DIR`/`ANGLE_LIB_DIR`. Kept
  intact pending the GLon12-on-GDKX result; revisit only if GLon12 fails.

## 8. Packaging / deploy tooling (Phase 5–6 — CLI tools, not link symbols)

Not header symbols, but the GDKX-gated commands we'll need to verify exist and
work as assumed in `deploy-xbox.yml` (disabled) and `CONSOLE_BRINGUP.md`:

| Tool | Assumed use | Status |
|------|-------------|--------|
| `makepkg` | build `.xvc`/loose package from `PackagingLayout.xml` + `MicrosoftGame.config` | ❓ |
| `xbapp install` / `xbapp launch` | deploy + launch on dev kit | ❓ |
| StoreBroker | submit to Partner Center — **may not handle GDK `.xvc`**; manual upload fallback | ⚠️ |
| `MicrosoftGame.config` schema | `Identity/@Name`, `@Publisher`, `StoreId`, `TargetDeviceFamily` (Scarlett) | ❓ validate against GDKX schema; identity injected from secrets (`PARTNER_CENTER_VALUES.md`) |

---

## Quick map: symbol area → code to touch

| Area | Files |
|------|-------|
| PLM (§1) | `xbox_main.cpp`, delete `xbox/smoke_stubs/*` for real build |
| Runtime init (§2) | `xbox_main.cpp` (new init/shutdown calls) |
| User (§3) | new `xbox/xbl_user.*`, `xbox_main.cpp` |
| Save (§4) | `save_storage.h/.cpp` (seam built; fill `NEWTONIA_XGAMESAVE` body), `savegame.cpp`, `highscore.cpp`, `preferences.cpp` |
| Safe area (§5) | `view/overlay.*` (logic already in place) |
| SDL glue (§6) | `sdl_gdk_stubs.cpp` (delete for console), `xbox/CMakeLists.txt` |
| Renderer (§7) | `xbox_main.cpp` `_GAMING_XBOX` present path, `xbox/CMakeLists.txt` |
| Packaging (§8) | `MicrosoftGame.config`, `PackagingLayout.xml`, `.github/workflows/deploy-xbox.yml` |

Keep this in sync as we touch each area — flip ❓/🔧/⚠️ → ✅ once confirmed
against the real GDKX, and note the GDKX edition in the commit.
