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

**Running the local build through Steam on Linux** (field-established 2026-09-05, Ubuntu snap Steam; the native client was not tried):
- **Build against the Steam runtime, not the host:** `./build_steam_sniper.sh` runs `make steam` inside Valve's `steamrt/sniper/sdk` container (docker or podman + `./sdk/`), mirroring deploy-steam.yml's build-linux job step for step — same apt set, SDL2 2.32.10 + SDL2_mixer 2.8.2 static at the depot's pins, libdatachannel via `build_netplay_deps.sh` against the runtime's OpenSSL 1.1 (keep the two in step when bumping a pin). Objects are `*.sniper.o` (`STEAM_OBJ_TAG`) so they never mix with the host build's; SDL and libdatachannel are cached under `./steam-sniper/` (rm -rf to rebuild); the checkout is mounted at its own host path so the baked rpaths hold at run time; `STEAM_APPID=` passes through (default the real id — app 480 "Spacewar" is an API-config app and confuses controller tests). A host `make steam` binary needs the host's glibc and cannot load inside Steam's runtime container ("version `GLIBC_2.43' not found"); the script prints the newest glibc symbol version the result needs — it must be ≤ 2.31 — and always relinks, because a host `newtonia-steam` newer than the sniper objects once made `make` skip the link and an evening went on launching the wrong binary.
- **Launch it through the LIBRARY entry, never a non-Steam shortcut:** set Newtonia's Launch Options to `/path/newtonia/steam_run_local.sh %command%`. Steam expands `%command%` to its full launch line ending in the depot binary; the script swaps that last argument for `./newtonia-steam` and execs the rest, so Steam runs the local build as the real app under the app's configured runtime, depot untouched (working dir = the depot, whose `audio/` is the same tree; libsteam_api via the `$ORIGIN` rpath into the checkout). Env vars in front work (`NEWTONIA_TRACE=/path/trace.log … %command%`). A non-Steam shortcut launches under the shortcut's id while `steam_appid.txt` announces the real app, and pads that work from the library never reached the game there — it is not a valid test bed for anything controller-related.
- **Getting output out:** Steam swallows the game's stdout/stderr, the snap's `/tmp` is private (`/tmp/snap-private-tmp/snap.steam/tmp/` on the host), and under the runtime container even unbuffered stderr didn't reach a `bash -c '%command% > file'` capture — `NEWTONIA_TRACE=/absolute/path` appends the startup trace (each init step, SDL's joystick enumeration, main-loop entry/return) to a file of its own. Steam's own launch log is `~/snap/steam/common/.local/share/Steam/logs/console-linux.txt` (`~/.local/share/Steam/logs/` on the native package); "Adding process … for gameID N" + "Removing process" a second later with no game output means the binary never reached `main` — check its glibc floor first.
- **Do not overwrite the depot's binary** to test locally; the wrapper above exists so that is never needed. And do not sign the same account in elsewhere while testing: a second login logs this client out, which shows as a login prompt after the game exits and reads like a crash.
- **Controller hot-plug is flaky on the snap client with the SHIPPED build too** (v1.57): a pad connected after launch was sometimes never seen by SDL while Steam showed it connected — reproduce on the default branch before blaming a branch, and check the F1 card's d-pad label to know which build is running ("dpup" = v1.57 and earlier, "DPAD UP" = the pad-glyph work onward).

`NEWTONIA_NO_STEAM=1` skips `SteamAPI_Init` (every backend takes its Steam-absent path) — the first bisection step when a launch through Steam misbehaves, since it separates "the Steam API" from "the binary/sandbox". `NEWTONIA_STEAM_INPUT=0` is the next step down: Steam stays up but the Steam Input pad backend is skipped, so the pads come through SDL exactly as before the action-set work — it separates "Steam Input" from "the rest of Steam".

**Steam Input (STEAMINPUT.md).** A Steam build launched by Steam takes the pads Steam presents from the Steam Input API and any it doesn't (Steam Input disabled for the pad) from SDL (`steam_input.cpp`, `pad.cpp`; the trace says `steam input: Init ok, sets Ship=N Menu=N` with non-zero handles, `controllers: … steam_input=1`, and one `joystick i: … steam_virtual=1` line per Steam-emulated pad SDL was told to skip). **Steam must be told the actions first**: copy `steam/game_actions_4536720.vdf` to `<Steam>/controller_config/game_actions_4536720.vdf` (`~/snap/steam/common/.local/share/Steam/controller_config/` on the snap client, `~/.local/share/Steam/controller_config/` native) and restart the Steam client — or upload it in the portal's Steam Input section. Without that the trace reads `sets Ship=0 Menu=0` then `action sets UNKNOWN … SDL pads`, and the game falls back to SDL for the whole run (the shipped build's behaviour). `make steam` still copies the file beside `newtonia-steam` (and into `Newtonia.app/Contents/Resources` on macOS) for the portal's bundled-config path, and the trace notes it `present`/`ABSENT` there; that copy alone registers nothing — `SetInputActionManifestFilePath` refuses it (it wants an input manifest, a different file). With the IGA registered, Steam still runs the pad on its **legacy gamepad template** until a layout that uses the game's actions exists — the trace then reads `handle N: layout has no Menu-set actions (legacy gamepad template) — SDL's emulated pad drives it`, the pad plays through SDL exactly like the shipped build, and every hint reads `-` only if something still asks the Steam side (it shouldn't: an un-adopted handle is not a pad). The moment a layout binds an action the trace reads `pad 0 adopted (handle N)` with the per-action origin dump under it, the SDL copy is released (`SDL pad 1 … released — the Steam backend drives handle N`), and the hints follow the layout. **A pad has no bindings until a layout exists for its type**: author the defaults once in the client's configurator (the pause menu's CONTROLLER LAYOUT row opens it; the layout to author is the "today's Xbox binding" column of STEAMINPUT.md §2 — A fire / B secondary / X / Y next / LB boost / RB teleport / L3 rotate / R3 controls / Start pause / Back menu / d-pad thrust-reverse-turn / left stick steer, and in the Menu set d-pad + stick nav, A confirm, B back, Start, Back exit, X paste, Y keyboard), export it, and set it as the per-type default configuration in the Steamworks portal beside the Custom Configuration (Bundled with Game) entry pointing at the manifest's depot-root path. Test only through the library entry (a non-Steam shortcut never reaches the API); the F1 card's chips and the FIRE/NEXT chips follow a remap made in the overlay within a frame — that is the field test for §4.

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

`generate_deck_announcement.py` (requires Pillow) renders the cover image for the Steam Deck / Steam Machine support post (`steam/announcements/steam-deck-steam-machine.md`; default 800×450, Steam's event cover size, size-parameterized): the same NEWTONIA title and starfield, with original line-art silhouettes of a Steam Deck (tiny in-game scene on its screen) and a Steam Machine underneath, captioned in the small Typer weight. It imports the online generator's font table, starfield and glow renderer and adds the caption glyphs (S M D C K H +) ported from `typer.cpp`. Deliberately NOT Valve's trademarked Steam / Steam Deck logos or the Deck Verified badges — those may only be used from the Steamworks branding assets under Valve's guidelines.
