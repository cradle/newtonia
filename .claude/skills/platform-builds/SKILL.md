---
name: platform-builds
description: Build Newtonia for a specific platform or regenerate generated assets — Steam local builds, macOS .app bundle, Web/Emscripten, Android, iOS, Xbox/GDK, and the generator scripts for sounds, the CA bundle, achievement icons, and the Steam announcement image. Use when building, packaging, installing to a device, or regenerating assets.
---

# Platform builds & asset generators

## Steam build (local achievement/overlay testing)
```sh
make steam       # Build ./newtonia-steam with -DSTEAM_BUILD
make steam-clean
```
Requires the Steamworks SDK unzipped at `./sdk/` (gitignored). Also drops `steam_appid.txt` (app 4536720, override with `STEAM_APPID=`) and the Steam runtime library beside the binary, so `./newtonia-steam` runs from the repo root with the Steam client logged in. Steam objects build as `*.steam.o`, never mixing with the plain build's objects. Never ship `steam_appid.txt` — real depots come from `deploy-steam.yml`. **Cross-platform:** `make steam` works on macOS (`libsteam_api.dylib`, also wraps a `Newtonia.app` for overlay/Game-Mode), Linux (`libsteam_api.so`, `$ORIGIN` rpath), and **Windows/MSYS2 MINGW64** (`steam_api64.dll` — MinGW links directly against the DLL, no `gendef`/`dlltool` import lib needed; the DLL rides beside `newtonia-steam.exe`, console subsystem so stdout is visible). The runtime lib is copied from the SDK's per-platform `redistributable_bin/{osx,linux64,win64}`.

**Adding the local build as a non-Steam game (Linux):** add **`steam-shortcut.sh`** as the game, not the binary. The shipped depot runs inside Steam's sniper container, which supplies freeglut, SDL2_mixer and the rest; a non-Steam shortcut gets no container, and under the **snap/flatpak Steam** the sandbox can't see the host's `/usr/lib` either, so the loader died before `main()` on `libglut.so.3`, then `libSDL2_mixer-2.0.so.0` (field, snap Steam, 2026-09-05 — Steam's own record of it is `~/snap/steam/common/.local/share/Steam/logs/console-linux.txt`, or `~/.local/share/Steam/logs/` on the native package). `make steam` on Linux therefore bundles the binary's whole `ldd` closure into `./steam-libs/` (`STEAM_LOCAL_LIBS`; gitignored, rebuilt on every relink), skipping only what must come from the running system — glibc + the loader, the GL / X11 / display-driver stack, and the audio/IPC client libraries (pulse, alsa, pipewire, dbus, systemd, udev: they talk to the sandbox's own sound and service plumbing, and a bundled `libpulse` failed on its private `libpulsecommon`, which it resolves through its OWN runpath into `/usr/lib/.../pulseaudio/`) (`STEAM_LIB_SKIP`); libstdc++/libgcc_s ARE bundled, since a host newer than the sandbox's base fails on `GLIBCXX` otherwise. The bundle is reached ONLY through the launcher's `LD_LIBRARY_PATH` — deliberately not an rpath: with `$ORIGIN/steam-libs` on the rpath, a direct launch under Steam's runtime compat tool loaded the bundle's older zlib/expat ahead of the host Mesa driver pressure-vessel injects, and GL init died before the first print (field, 2026-09-05; the Mesa dependencies are now on the skip list too). **Two routes, don't mix them:** (1) no compat tool → shortcut to `steam-shortcut.sh` (launcher + bundle); (2) "Steam Linux Runtime 3.0 (sniper)" compat tool → shortcut to `newtonia-steam` itself, no launcher, no bundle — the container supplies every library, exactly as for the shipped depot, and this is the better route for a sniper-built binary. **Check which binary you have** before blaming the launcher: `objdump -T newtonia-steam | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1` must be ≤ 2.31 for a runtime build (the script prints this). An evening went on a shortcut that "silently exited 0" under the snap client's compat tool — it was the HOST binary all along: the sniper build's `make` saw a newer `newtonia-steam` from an earlier host `make steam` and skipped the link (fixed: the script removes it first), and Steam's launch chain dropped the loader's `GLIBC_2.43 not found` on the floor. A wrapper script as the shortcut target, writing `ldd` + the game's output to a file from INSIDE the runtime, is what exposed it — keep that trick. The shipped depot links libdatachannel against the runtime's OpenSSL 1.1; a `libssl.so.3` dependency is another tell of a host build. Also: a terminal launch beside the running client exercises the Steam API fine (init, Steam Input attach, achievements, presence), but Steam Input does not drive a pad for a process Steam didn't launch, so the live-layout path needs a Steam launch — the shortcut, or the beta depot. libdatachannel still resolves through its absolute `netplay-libs` rpath (the sandbox can see `$HOME`). If it still dies silently, `ldd ./newtonia-steam | grep 'not found'` from a wrapper the shortcut launches lists exactly what the sandbox is missing.

**But the bundle cannot carry glibc.** A host whose glibc is newer than the sandbox's base (Ubuntu 26.04's 2.43 vs the snap's, field 2026-09-05) fails on `version 'GLIBC_2.43' not found (required by newtonia-steam)`, and no amount of copying fixes that. The answer is the depot's own answer — build against the runtime: **`./build_steam_sniper.sh`** runs `make steam` inside Valve's `steamrt/sniper/sdk` container (needs docker or podman + `./sdk/`), mirroring deploy-steam.yml's build-linux job step for step (same apt packages, SDL2 2.32.10 + SDL2_mixer 2.8.2 static at the depot's pins, libdatachannel via `build_netplay_deps.sh` against the runtime's OpenSSL — keep the two in step when bumping a pin). Objects are `*.sniper.o` (`STEAM_OBJ_TAG`) so they never mix with the host build's, the SDL/libdatachannel builds are cached under `./steam-sniper/` (rm -rf to rebuild), and the outputs are the same files `make steam` writes, so `steam-shortcut.sh` launches the result unchanged. In that build libstdc++/libgcc_s are deliberately left OUT of `steam-libs/` (`STEAM_LIB_SKIP_EXTRA`): the runtime's older copies loaded first would starve the sandbox's own Mesa of the GLIBCXX it needs, and the sandbox's are new enough for a runtime-built binary by construction. Under rootless podman the container's root IS the caller, so the script must NOT chown outputs to the host uid (that maps onto a subordinate uid and strands the tree — field 2026-09-05; repair with `podman unshare chown -R 0:0 .`); it chowns only when `/proc/self/uid_map` says the runtime is real root (docker). Since it's the depot's exact environment, the same binary also runs under Steam's "Steam Linux Runtime 3.0 (sniper)" compatibility tool with no bundle at all, when the shortcut's properties can be edited.

**Test under Valve's app 480 ("Spacewar"), not the real app id, when launching as a non-Steam shortcut.** With `steam_appid.txt` = 4536720 the process announces itself as the REAL Newtonia the moment `SteamAPI_Init` runs, and Steam applies that app's controller config, game recording, presence and running-state tracking to a binary it never installed (the console log's "Adding process … for gameID 4536720" is that; field, snap Steam, 2026-09-05). `make steam STEAM_APPID=480` / `STEAM_APPID=480 ./build_steam_sniper.sh` registers as the test app every account owns instead — achievements/stats calls then target Spacewar's (undefined, so no-ops), Steam Input runs with a default layout, and nothing touches the real app's state. Use the real id only when testing achievements or presence, and `rm steam_appid.txt` when switching (the Makefile writes it only when missing; the sniper script drops it every run).

`NEWTONIA_TRACE=1` prints one unbuffered stderr line per desktop startup step (Steam init, Steam Input, achievements, invites, prefs, window+GL, controllers, main-loop entry/return) — for a launch that exits before its first stdout print. Bisecting a launch that misbehaves under Steam: `NEWTONIA_NO_STEAM=1` skips `SteamAPI_Init` (every backend takes its Steam-absent path), `NEWTONIA_NO_STEAM_INPUT=1` skips only `ISteamInput::Init` (pad hints fall back to SDL's type) — set them in `steam-shortcut.sh` for one launch each; a wrapper the shortcut launches can also redirect the game's stdout/stderr to a file, since Steam swallows both.

To re-test earns from scratch: run once with `NEWTONIA_RESET_STEAM_STATS=1` (wipes the account's Steam achievements/stats via `ResetAllStats`, then quit), and delete the local lifetime file `~/Library/Application Support/cc.gfm/newtonia/stats.dat` — otherwise `specials_7` re-unlocks on the first kill from the banked type mask.

## macOS App Bundle
```sh
make osx      # Build universal arm64+x86_64 .app bundle
```

Signing entitlements live in `macos/` (`Entitlements.plist`, `EntitlementsDevID.plist`).

## Web (Emscripten)
```sh
make web        # Build WebAssembly output to web/dist/
make web-clean  # Remove web build artifacts
```

`make web NETPLAY=0` force-disables netplay in the web build (`-DNEWTONIA_NET_DISABLED`: ONLINE row hidden, `?code=` invite codes drained and dropped, transport/signal factories return null — the signaling Worker and TURN are never contacted; the backend still compiles, so the source set is unchanged). **The PUBLIC web deploys build this way** — web.yml (GitHub Pages) and deploy-itch's `html5` channel — permanently, by the pricing decision in NETPLAY.md (the game is paid on Steam/iOS, so free browser co-op would undercut them). Browser co-op was to arrive via the paid `newtonia-online` itch project, which deploy-itch still builds netplay-ON and pushes on every release — but that project is **unpublished and parked**: itch cannot purchase-gate HTML5 embeds, so the push is inert until server-side purchase verification exists (NETPLAY.md Gate 5, task #156).

Requires Emscripten (`emcc`) and TypeScript compiler (`tsc`) on PATH. The web frontend is TypeScript: `tsc -p web/tsconfig.json` compiles `web/main.ts`; `web/shell.html` is the Emscripten shell file. The build links `-lidbfs.js` for IndexedDB persistence and preloads audio assets (`--preload-file audio@audio`). `web_main.cpp` mounts IDBFS asynchronously and only starts the game loop after `web_on_idb_ready()` fires from JS.

Output layout (see `web/README.md`): the marketing landing page (`web/site/`, including the GitHub Pages `CNAME`) is copied to the `web/dist/` root; the playable game builds into `web/dist/play/`. The shell's "back to site" link points at https://newtonia.metonymous.com and opens in a new tab when embedded off-domain (e.g. the itch.io iframe). The build sets `-s GROWABLE_ARRAYBUFFERS=0` explicitly — the emscripten 6.0.2 default-on setting breaks Firefox (its TextDecoder rejects views over resizable ArrayBuffers).

## Android
```sh
make android          # Copy audio assets + build debug APK (app-debug.apk)
make android-install  # Build + install + (re)launch on a connected device / emulator (adb)
# or, directly:
cd android && ./gradlew assembleDebug
```

`make android`/`make android-install` are thin wrappers over Gradle that first copy `audio/` into `android/app/src/main/assets/` (matching the CI "Copy audio assets" step) so the APK ships its sounds; `make android-clean` runs `gradlew clean` and removes the copied assets. SDL2/SDL2_mixer must still be cloned as siblings to the repo root first (see `.github/workflows/android.yml`). The native build uses the root `CMakeLists.txt` (a generic CMake build that globs all sources, excludes the desktop `glut.cpp` entry point, and clones SDL2/SDL2_mixer from GitHub). Requires Android NDK 28.2.13676358 (r28 — compiles native libraries with 16 KB-aligned ELF LOAD segments by default, required for Google Play 16 KB-page-size devices) and CMake 3.22.1 (both set in `android/app/build.gradle`; compileSdk/targetSdk 36, minSdk 21; ABIs arm64-v8a, armeabi-v7a, x86_64). The CMake target is `libnewtonia.so` (shared library). AGP 8.7.3 / Gradle 8.9 (AGP ≥ 8.5.1 16 KB-aligns the uncompressed `.so` in the APK zip). The 16 KB linker flags are also set explicitly in the root `CMakeLists.txt` as a belt-and-suspenders safeguard.

## iOS
```sh
make ios          # Signed device .app via xcodebuild (no Xcode GUI setup)
make ios-install  # Build + install + launch on a connected device (devicectl)
```

Command-line device build through the XcodeGen project. One-time setup: `brew install xcodegen`, `./build_sdl_deps_ios.sh device` (static SDL2/SDL2_mixer → `sdl-libs-ios/`), `./build_netplay_deps_ios.sh device` (→ `netplay-libs-ios/`), and sign the Apple ID into Xcode once (Settings → Accounts) so `-allowProvisioningUpdates` can mint the dev profile from the CLI (`IOS_TEAM ?= 4RWPRHJG6D`). Netplay defines/headers/archives are passed as build settings automatically — the same hand-off `deploy-ios.yml` uses. Debug signs with `ios/EntitlementsDev.plist`; `IOS_CONFIG=Release` for an optimized build; `IOS_DEVICE=<name-or-udid>` overrides device auto-detection. Audio ships via the `project.yml` folder resource (so the bundle is complete *before* signing — `deploy-ios.yml` verifies rather than copies).

The Xcode project is generated by XcodeGen from `ios/project.yml` (the `.xcodeproj` is gitignored — see convention 11): `cd ios && xcodegen generate`, then open `ios/Newtonia-iOS.xcodeproj` in Xcode if a GUI session is ever wanted. For simulator builds see `ios/README.md`.

## Xbox / GDK (Windows)
```sh
cmake -B xbox/build-desktop -S xbox -A Gaming.Desktop.x64
```

`xbox/CMakeLists.txt` builds for GDK Desktop (Gaming.Desktop.x64) and Xbox Series (Gaming.Xbox.Scarlett.x64) using the VS2022 MSBuild platform registration installed by GDK 2510+ — no separate toolchain file. Renders via OpenGL ES 2 through ANGLE (libEGL/libGLESv2 from the ANGLE.WindowsStore NuGet package — not bundled with the GDK; located via `ANGLE_INCLUDE_DIR`/`ANGLE_LIB_DIR` or `GDK_ROOT`). See `xbox/PORT_PLAN.md` for the full port plan. Uses static MSVC runtime (`/MT`); `xbox/sdl_gdk_stubs.cpp` provides GDK PLM stub symbols; packaging config in `xbox/MicrosoftGame.config` and `xbox/PackagingLayout.xml`.

**Xbox development is deferred to a private repo (2026-07-30).** All remaining Xbox work — console bring-up, the rendering spike, cert features, packaging/submission, the GDK achievements backend, Xbox netplay, and even the non-NDA GDK Desktop manual test pass — is owned by `cradle/newtonia-xbox`, not by this repo (decision + ownership split: `xbox/PRIVATE_REPO.md`). What stays here is frozen scaffolding: the files above, the platform-neutral seams the private backends land against, and the two CI canaries (`xbox-dev.yml`, `xbox-console-smoke.yml`) — **keep those green**, since they guard shared code, but don't schedule Xbox port work here. The `xbox/*.md` documents are reference and handoff material, not task lists. Non-NDA outcomes come back as ordinary upstream PRs.

## Sound assets
`generate_sounds.py` procedurally generates the WAV files in `audio/`.

## CA roots
`generate_ca_bundle.py` regenerates `net_ca_bundle.cpp` — the Mozilla root program (curl's `cacert.pem`, 119 certificates) embedded as chunked string literals, which `net_tls.cpp` writes to the pref path for libdatachannel to verify the signalling/leaderboard sockets against (LEADERBOARD.md S1). Deliberately the full bundle rather than Cloudflare's current four CAs: they rotate per certificate, so a narrow set is a silent tripwire, and the win here is verification, not pinning. The output is committed so no build needs the network; rerun after a root-program change (a year or two apart), then run `./test/tls/run.sh`.

## Achievement icons
`generate_achievement_icons.py` (requires Pillow) procedurally generates the 256×256 achieved/locked Steam achievement icon pairs into `steam/icons/` and the 512×512 Game Center variants (achieved art only — Game Center renders its own locked state) into `gamecenter/icons/`, porting the in-game glyph constructions (ship/station/asteroid meshes, Typer font, in-game colours) so the icons match the game's look. One deterministic scene function per §5 symbolic ID — rerun after any achievement list change.

## Steam announcement image
`generate_online_announcement.py` (requires Pillow) reproduces the "NEWTONIA ONLINE" Steam event capsule (default 400×225, size-parameterized): the in-game Typer segment font (glyph geometry ported vertex-for-vertex from `typer.cpp`) rendered as glowing green strokes over a seeded dark starfield, "NEWTONIA" large with "ONLINE" right-aligned beneath. Deterministic (fixed RNG seed) so the starfield is identical on every run. Rerun after any change to the title text, colours, or the Typer glyph table.
