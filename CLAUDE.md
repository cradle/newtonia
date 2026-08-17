# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Newtonia is a top-down 2D space shooter written in C++ using SDL2 and OpenGL. It supports single-player and local co-op for up to 4 players (FOURPLAYER.md; P3/P4 join by controller) and targets multiple platforms: desktop (macOS, Linux, Windows), mobile (iOS, Android), web (WebAssembly), Xbox/GDK, and Steam.

## Build Commands

### Desktop (macOS / Linux)
```sh
make            # Build native executable: ./newtonia — NETPLAY ON by default
make NETPLAY=0  # Netless binary (no libdatachannel needed)
make clean      # Remove build artifacts
```

Compiler: g++ with `-Wall -O3 -std=c++11`. Sources include root, `weapon/`, and `view/`.

**Every build path stamps a version** into `NEWTONIA_VERSION_STRING`, which lands in each replay file's header as the leaderboard **season key** (REPLAY.md R4). Resolution order is the same everywhere — `-DNEWTONIA_VERSION` / `NEWTONIA_VERSION=` , then the environment, then `git describe --tags --abbrev=7 --dirty=+ --always` — implemented in the Makefile (desktop/Steam/web/osx), the root `CMakeLists.txt` (Android) and `xbox/CMakeLists.txt`; iOS takes it as an xcodebuild setting (`NEWTONIA_VERSION_DEFINE`, the same hand-off `NEWTONIA_NET_DEFINE` uses, wired in `ios/project.yml`). On **tag builds** the four deploy workflows all stamp the root **`SEASON` file's content** (`s1`, single line, bumped deliberately when the game changes significantly — LEADERBOARD.md season decision), so one release stamps one identical string everywhere and seasons rotate on gameplay changes, not release cadence; manual dispatches stamp honest non-release buckets instead (branch name; iOS beta-worker its v<maj.min>.9xx sentinel, Android dispatch 0.0.1-sha) so ad-hoc builds never chart on the live board. Play's `versionName` and App Store Connect's `CFBundleShortVersionString` need the bare dotted number and are deliberately not what gets stamped. Workflows pass the stamp explicitly because `actions/checkout` is shallow and fetches no tags. The field holds **23 chars and truncates silently**, which is why the describe format is the short one. Unresolvable falls back to `replay.h`'s `"dev"`, so this can never fail a build — but note a hand-rolled `make CFLAGS=...` override drops the stamp with the rest of `CFLAGS`. Both CMake paths resolve the stamp at *build* time into a generated header (`cmake/version_stamp.cmake`, write-on-change), so an existing build tree picks up new tags, commits, checkouts, packed refs and worktrees without a reconfigure; the Makefile's `version.stamp` covers `replay.o`/`replay.steam.o`.

**Netplay builds by default** (the old opt-in `NETPLAY=1` still works and is
now redundant). The default needs libdatachannel at `./netplay-libs` — build
it ONCE with `./build_netplay_deps.sh` (`--universal` for `make osx`; needs
`cmake` + OpenSSL headers on top of the game's own deps — see the per-OS
dependency sections below). A missing prefix is a hard `make` error (never a
silent netless fallback); `make NETPLAY=0` is the explicit opt-out.
`make web` / `make android*` don't
need the prefix (web's backend is unconditional; Android builds via Gradle).

**libdatachannel is PATCHED** — `patches/libdatachannel-ws-ca-cert.patch`
(9 lines) adds `caCertificatePemFile` to the C `rtcWsConfiguration` and lets
Windows verify when a CA is supplied. Without it the signalling and
leaderboard sockets can verify nothing, and they carry the platform
verification credential (LEADERBOARD.md S1). Every path that builds the
library applies it — both `build_netplay_deps*.sh`, both FetchContent
`CMakeLists` (through `cmake/apply_patch.cmake`, idempotent on re-populate),
and the four workflows that clone it themselves (`windows`, `deploy-steam`,
`ios`, `deploy-ios`). A prefix built WITHOUT the patch fails to compile the
game on the unknown field, which is deliberate: an unverified socket must
never be the quiet outcome. **Re-check the three hunks on any libdatachannel
bump.** The roots themselves ship in the binary (`net_ca_bundle.cpp`, written
to the pref path by `net_tls.cpp` on first connect) because MbedTLS reaches
no system trust store anywhere and OpenSSL reaches none on Windows;
`NEWTONIA_NET_TLS_INSECURE=1` turns verification off for field debugging and
is set by no shipped build.

#### Steam build
See the `platform-builds` skill (`.claude/skills/platform-builds/SKILL.md`) for local Steam testing — SDK setup, per-OS runtime libs, `NEWTONIA_RESET_STEAM_STATS`.

#### Linux dependencies
```sh
sudo apt-get install -y libsdl2-dev libsdl2-mixer-dev freeglut3-dev
sudo apt-get install -y cmake libssl-dev        # only for ./build_netplay_deps.sh
```

#### macOS dependencies
```sh
brew install sdl2 sdl2_mixer
brew install cmake openssl@3                    # only for ./build_netplay_deps.sh
```
GLUT ships with Xcode Command Line Tools (`xcode-select --install`).

#### Windows (MSYS2/MinGW64)
One-time setup (installs MSYS2 via winget, all packages, netplay deps, and
builds + selftests; idempotent, safe to re-run):
```powershell
./setup_windows_build.ps1                # add -SkipNetplay for a plain build
```
Day-to-day, from the **MSYS2 MINGW64** shell (not "MSYS2 MSYS" — only
MINGW64 has g++/SDL on PATH), or from PowerShell via
`$env:MSYSTEM='MINGW64'; C:\msys64\usr\bin\bash.exe -lc 'cd "$PWD" && make -j8'`:
```sh
make -j8              # netplay build, the default (needs ./netplay-libs — see below)
make NETPLAY=0 -j8    # plain netless build
```
The Makefile's `_NT` branch mirrors `.github/workflows/windows.yml`: fully
static `newtonia.exe` (no MinGW DLLs needed), GUI subsystem so stdout only
shows when piped from a shell (e.g. the `NEWTONIA_NET_SELFTEST=1` gate).
`./build_netplay_deps.sh` has a matching Windows branch: static
libdatachannel + vendored archives copied into `netplay-libs/lib/*.a`, all
linked in one `--start-group` with msys2's static OpenSSL. `%zu` in
`SDL_Log` formats warns (and misprints) under MinGW — cast to `unsigned`
and use `%u`. **Do not name a local `near` or `far`**: the Windows headers
still define those 16-bit segment qualifiers as macros, so the code
miscompiles with an error that names neither the variable nor the real
cause (a `std::vector` called `near` surfaced as "no matching function for
call to `unordered_map::find(<lambda()>)`"). Bitten twice — once in
`glgame.cpp`'s spawn-distance check, once in the elastic-collision grid
pass. Neither the desktop syntax check nor a Linux build catches it; only
windows.yml does.

#### Syntax-check without a full build
The pre-commit hook in `.claude/settings.json` runs this automatically on staged files:
```sh
g++ -std=c++11 -fsyntax-only -I. -I/usr/include/SDL2 <file.cpp>
```

**This desktop check does NOT exercise Android-only code.** It compiles without
`__ANDROID__`, so every `#if defined(__ANDROID__)` branch is skipped and the
Play Games TUs (`play_games_*.cpp`, guarded on `__ANDROID__ && PLAY_GAMES_BUILD`)
collapse to empty translation units. A change to `android_main.cpp` or any
Android-guarded code can pass the desktop check and still break the NDK build —
two ways this has actually bitten: a block-scope `extern "C"` (illegal outside
namespace scope; the NDK Clang rejects it) and a C-vs-C++ linkage mismatch on a
free function shared across TUs. Neither showed up until `android.yml` ran.

**Android compile-check on Linux (no NDK needed).** For a fast parse/semantic
check of an Android translation unit with the real headers and the Android
build's defines:
```sh
sudo apt-get install -y libsdl2-dev libsdl2-mixer-dev libgles2-mesa-dev  # SDL.h, SDL_mixer.h, GLES2/gl2.h
JNIDIR=$(dirname "$(find /usr/lib/jvm -name jni.h | head -1)")            # jni.h ships with the JDK
g++ -std=c++11 -fsyntax-only -D__ANDROID__ -DPLAY_GAMES_BUILD \
    -DNEWTONIA_NET_IDENTITY_BACKEND -DNEWTONIA_NET_VERIFY_BACKEND -DNEWTONIA_NET_RTC \
    -I"$JNIDIR" -I"$JNIDIR/linux" -I. -Iview -Iweapon -I/usr/include/SDL2 \
    android_main.cpp
```
Run this after ANY edit to `android_main.cpp` or an Android-guarded/`play_games_*`
TU. **Caveat:** it is a host-toolchain PARSE check — it catches syntax, type,
and linkage-specification errors (e.g. the block-scope `extern "C"`) and missing
declarations, but NOT cross-TU **link** errors (undefined-symbol / name-mangling
mismatches) or NDK/ABI issues. For those the only real gate is a full Android
build (`make android`) or the `android.yml` CI.

#### Headless testing, other platforms & asset generators
The Xvfb/xdotool headless runtime-testing driver technique (per-step liveness checks, screenshots, gdb backtraces, the `NEWTONIA_BETA`/`NEWTONIA_START_GENERATION`/`NEWTONIA_ALL_WEAPONS` test hooks) lives in the `headless-testing` skill; TESTING.md holds the full test inventory. Steam, macOS bundle, Web/Emscripten, Android, iOS, and Xbox/GDK build instructions plus the asset-generator scripts (`generate_sounds.py`, `generate_ca_bundle.py`, `generate_achievement_icons.py`, `generate_online_announcement.py`) live in the `platform-builds` skill. Xbox work remains deferred to the private repo (`xbox/PRIVATE_REPO.md`) — keep the two xbox CI canaries green, but don't schedule Xbox port work here.

#### Screenshot harness (`NEWTONIA_SHOT` — see shots/README.md)
`NEWTONIA_SHOT=out.png` turns the desktop binary into a one-shot scene
renderer for store/marketing assets: scene scripts (`shots/*.shot`) compose
menu or game worlds (asteroid types, hazards, enemies, Typer text captions,
touch OSD via `NEWTONIA_FORCE_TOUCH`), rendered at any size, deterministic
per platform, sandboxed from all player data. Drivers: `shots/run.sh`
(Xvfb), `shots/run.ps1` (Windows/GPU), `shots/mobile.sh|ps1` (store
screenshot sizes). Full DSL reference and gotchas in `shots/README.md`;
approved renders are committed under `shots/out/`.

#### Video capture (`NEWTONIA_VIDEO` — see shots/README.md, video_capture.h)
`shots/video.sh` renders a **recorded replay** (REPLAY.md) to an MP4 — store
trailer footage rebuilt from the command line, with `--info` to read a run's
header and `--start`/`--duration` to cut a highlight (the skip-ahead doesn't
render). A render, not a screen recording: the timestep is fixed, so a slow
headless context still yields a smooth 60 fps and the same file renders the
same frames every time. **Picture and sound are two passes** — SDL holds the
mixer lock across the audio callback and the game takes it on every sound
call, so pacing the mixer from inside the callback deadlocks the game thread
(measured: 52 s of a 53 s render inside `tick`). The video pass runs flat out
and silent; the audio pass replays the same records without drawing and
throttles itself to the device's real-time rate; the script muxes them. Two
wall-clock reads in the render path had to become sim time for this — the
`GLTrail` spawn cadence and `GLGame::draw`'s camera-smoothing `frame_delta` —
both of which were also wrong under the time-scale keys and a replay's 4x
fast-forward. Verified by `test/e2e/video.sh`.


## Architecture

### Game Loop

- `StateManager` owns the current `State` and dispatches input (keyboard, controller, touch, mouse)
- Each frame: `state->tick()` (physics/logic), `state->draw()` (rendering)
- Fixed timestep: `step_size = 8ms`; delta accumulates and runs discrete steps
- World starts at 2500×2500 units (set via `WrappedPoint::set_boundaries`) and grows each generation

### Level / Generation Progression

When all killable asteroids **and all hazards** (pulsar/comet/seeker) are destroyed, a 5-second countdown (tick sounds) runs, then `generation` increments and the level rebuilds (`glgame.cpp`):

- World grows by 50×50 per generation; at generation 14 it instead grows by 3000×3000
- Asteroid count: `default_num_asteroids + generation * extra_num_asteroids`
- Special asteroid types unlock by generation: reflective ≥ 2, teleporting ≥ 3, invisible ≥ 4, quantum ≥ 5, tough ≥ 6, armoured ≥ 7, phasing ≥ 8 (counts scale with generation)
- `Hazard` obstacles fill the otherwise-quiet mid-game levels (`hazard.h/cpp`, spawned by `GLGame::add_hazards()`, counts scale with generation like the specials): pulsar ≥ 9, comet ≥ 11, seeker ≥ 12
- `GLMiniStation` (mini-station) spawns from generation ≥ 10
- Black hole spawns at world centre from generation ≥ 13
- `GLStation` (enemy station) spawns from generation ≥ 14
- Pickups are cleared, the starfield and grid are rebuilt, players respawn, and progress is auto-saved
- Every level that introduces a new object type gets an intro screen — the `Intro` state (`intro.h/cpp`; asteroid specials at 1–8, pulsar at 9, mini-station at 10, comet at 11, seeker at 12, black hole at 13, station at 14; a new game starts straight into play): the world freezes and the object spins centre-screen with its name and a flashing "PRESS FIRE TO START" ("TAP FIRE TO START" in touch mode); any player's shoot input (key, controller A/right trigger, the touch fire button — not a tap anywhere) dismisses it, and the touch OSD is drawn on the intro so the fire button is findable, or it auto-starts after 5 s (`Intro::auto_start_ms`) with no input (`GLGame::maybe_start_intro()` decides whether one is due and hands the game to a new `Intro`; not persisted in saves — resuming a save never re-shows one); a looping title-style tune (`audio/intro.wav`) plays on its own channel while other sound channels stay paused, halted on dismissal

## Key Systems

### State Machine

`StateManager` (`state_manager.h/cpp`) drives top-level transitions. Each state inherits from `State` (`state.h/cpp`):

- Pure virtual: `draw()`, `keyboard()`, `keyboard_up()`, `controller()`, `tick()`
- Virtual: `touch_tap()`, `back_pressed()`, `resize()`
- **Generalised nav input**: menu-ish screens keep ONE decision ladder (`Menu::nav_input`, `NetLobby::nav_input`) speaking logical keys — w/a/s/d move, Enter/space confirm, Esc backs out one level. Keyboard reaches it via `State::nav_key` (arrows→WASD) after per-platform touch filtering; controllers reach it via `State::nav_key_from_controller` (dpad + left stick with shared arm/release hysteresis → wasd, A/Start/RT → Enter, B/Back → Esc, and the source pad returned so Menu binds it to P1 on confirm). New screens should follow this pattern rather than hand-rolling a controller handler; richer pad semantics (gameplay, the lobby's CodeEntry picker) claim their events before falling back to the translator
- **Shared selection primitives** (`menu_select.h/cpp`): the ladders don't spell out the key vocabulary or the movement themselves — `MenuSelect::is_up/is_down/is_left/is_right/is_confirm/is_back` are the whole vocabulary, `move(key, sel, count)` (and `move_within(key, sel, lo, hi)` where the highlight may rest off the list, as the lobby's LAN rows do at -1) does the clamped step, and `draw_row(y, text, size, selected)` / `draw_row_cursor(selected, left_x, right_x, y, size)` draw the cursor. Every highlighted row in the game goes through these — main menu, options, replays, the Yes/No confirms, the lobby chooser and LAN list, the pause menu — so a screen can't drift from the rest by hand-rolling its own (which is exactly how a lone `> ` prefix survived on two lists after the menus had moved on). `Typer::cursored` stays the underlying string form, for labels with no row geometry (the touch EXIT TO MENU bands)
- **Desktop taps (Steam Deck touch)**: the desktop entry point forwards left-button releases as `touch_tap` (`glut.cpp` `mouse()`, which doubles as plain mouse support), and — Linux only — listens for **XInput2 touch directly**: the Deck's touchscreen arrives as XI2 touch events that never become clicks for the freeglut window (neither gamescope nor Plasma pointer-emulates them to it), so `glut.cpp` opens a second X connection announcing XI 2.2, selects `XI_TouchBegin/End` on the GLUT window, polls it each tick, and forwards a sequence's end through the same `forward_tap` as a click release (time+position dedup guards the double-delivery case; links `-lXi`). `NEWTONIA_TAP_DEBUG=1` overlays a persistent input-event status line for field diagnosis. **Gaming Mode needs touch passthrough** (field-verified 2026-07-25): gamescope's default touch→mouse emulation delivered NOTHING to this title's window (no clicks, no XI2 touch, not even raw root events — the finger never reached gamescope's X server), while Desktop Mode Plasma delivered XI2 touch that the listener consumed correctly. The fix is Steam-side: the Steamworks portal per-app **Steam Deck Touchscreen** dropdown must be set to **Touch API Pass-through** (per-user equivalent: layout Always-On Command → System → Touchscreen Native Support), which makes gamescope forward native touch — the listener's input. Set in the portal alongside any config publish; there is no code-side substitute. Menu screens therefore hit-test taps in BOTH layouts: the touch layouts as before, and the desktop layouts via the same shared draw/hit-test geometry (`menu_row_at`, `opt_row_at` + `DESK_OPT_TOP/BOTTOM`, `desktop_confirm_pick` for the stacked Yes/No confirms — one geometry definition feeds draw and hit-test, the TapBand rule). In-game clicks stay inert on desktop (`GLGame::touch_tap` guards on `is_touch_mode()`); the lobby's handler is desktop-safe as-is (its exit band is drawn on desktop too, Choose's half-split matches the drawn rows, share/soft-keyboard paths no-op off touch), and its waiting-room list answers the pointer through `NetLobby::list_row_at` — the row geometry **recorded by the draw** (`list_origin_y_`/`list_line_h_`/`list_line_sz_`, plus `host_peer_line0_`/`host_start_line_`), not re-derived, because that list shrinks its step and size to fit once the roster grows. **A band drawn for a finger is not a hit-test for a mouse**: `TapBand::for_pointer()` drops the finger padding and the `to_bottom` edge run off touch, and every hit-test of the shared exit band goes through it (`NetLobby::touch_tap`, `menu_exit_hit()`) — with the raw band the bottom ~19% of the window was a menu exit, so a click below the lobby roster left the room (field, 2026-08-13). The DRAW keeps the untightened band, so no label moves
- States call `request_state_change()` to transition. `request_state_change(next, true)` transfers ownership of the outgoing state to the next one — the `StateManager` skips its usual `delete` — and `clear_state_change()` resets a stale transition when such a state is later reinstalled

There are three states:
- **Menu** (`menu.h/cpp`) — main menu and options screen, animated starfield, touch support. On non-touch platforms it opens on an attract screen (flashing "PRESS ENTER/START") dismissed by Enter/Space/controller Start. Esc or controller Back opens a quit confirmation (compiled out on web with `__EMSCRIPTEN__`; Android/Xbox reach it via `back_pressed()`). Selecting NEW GAME while a save exists shows a "New game?" YES/NO confirmation (NO is the default; keyboard/controller stack YES above NO, touch puts YES on the left half and NO on the right). Options screen (a data-driven row list, `opt_row`): per-player P1–P4 sensitivity (SLOW–MAX, 0.5–2.0, 5 steps), camera smoothing (OFF–MAX, 0.0–0.010, 5 steps) and camera (FIXED/ROTATE, 2 steps) rows, star density (MINIMAL–FULL, 0.1–1.0, 5 steps). Desktop/controller: one line per row, three columns (name left / numbered steps mid / value right), left/right adjusts, Esc/back closes. The full desktop list is 15 rows (14 without the netplay-only LEADERBOARD UPLOAD row) and stays a flat list — `opt_row_center` compresses the fixed band, and the landscape band is resolution-independent, so only row COUNT tightens it (FOURPLAYER.md D9/O5, verified via `shots/options.shot`). The tight axis is horizontal: the name column clears the step column by two thirds of a glyph at the longest name, so count glyphs when adding a row. Touch: P2 rows excluded (mobile shows P1 + shared options), one row each with the name left and value right, tap to cycle, a EXIT TO MENU band exits
- **GLGame** (`glgame.h/cpp`) — in-game; owns all game objects; handles asteroid spawning, pickup drops, split-screen (orientation strips at 2 players, a 2x2 grid at 3-4 — `viewport_rect`; local seats capped by `LOCAL_PLAYER_CAP`), pause, auto-save. Game over transitions back to Menu (no separate game-over state). **Pause menu**: on desktop/controller the pause screen is a selectable menu carrying the shared `Typer::cursored` marks — RESUME / EXIT TO MENU, with **PLAYERS** between them offline (`pause_row_count()`/`pause_row_at()` map a selection index onto the right action, so the drawn rows and the ladder can't disagree). `pause_nav()` is its `Menu::nav_input`-style ladder (w/s + arrows move, Enter/space/pad-A confirm), `pause_menu_active()` gates it (never on touch, on a finished game, in a replay, under the help card, or while the seat roster is up). It draws as ONE full-window pass from `GLGame::draw` (not per viewport — there is one pause state and one cursor, so a split-screen copy per cell was four menus answering the same keys), dimming the frame behind the live cursor menu. **Seat roster** (offline, `Overlay::seat_roster` + the `roster_*` members): the PLAYERS row opens a full-window list of seats 1..4 showing what drives each — a keyboard cluster labelled from its own bindings ("WASD"/"IJKL"), `PAD n`, or NONE — plus an ADD row while under `MAX_PLAYERS`, and a trailing **BACK** row (the way out as a selectable row: the screen used to end in an "ESC BACK" hint — a key spelled out under a list whose every other row is picked with the cursor, and the only exit the cursor could not reach; Esc still works). Left/right cycles a seat's input through {none, every bound `PlayerKeys` slot, every connected pad} and an unassigned pad claims the highlighted seat by pressing any button (press-to-claim); assigning to the ADD row seats a new player. An input drives exactly one ship — applying it strips it from whatever seat held it — which is why `GLShip` carries an explicit `keymap_slot()`: seat index and keymap slot are deliberately separate, so "player 3 flies with WASD" is just seat 3 holding slot 0 (the old seat-N-gets-slot-N wiring was the only reason P3/P4 couldn't use a keyboard). Assignments are runtime-only (not in the savegame). **Online the same screen is the host's kick UI** (FOURPLAYER.md O3): `roster_available()` also opens for `NetHost`, remote rows name the pilot and offer **two removals** instead of the rebind (no ADD row — online seats fill from the room), and the lobby's waiting-room roster grew the matching selection (resting at -1, where confirm still means START GAME). **KICK and BAN are separate actions**, picked with the same left/right that cycles a local seat's input (meaningless on a peer row): a KICK removes them and lets them rejoin with the room code — the ordinary case, where the fix for someone stuck at the wrong seat should not also be a punishment — while a BAN additionally refuses their next handshake. One code path with a flag (`net_kick_peer(p, ban)` / `host_kick_selected(ban)`); `roster_ban_`/`host_ban_` carry the choice and reset to KICK whenever the highlight moves. Only the CURSOR's row renders the picked action (`< KICK >` / `< BAN >`, `[CONFIRM …]` when armed) — the others show a static `KICK / BAN`, since the flag describes the highlighted row and rendering it everywhere put `< BAN >` on rows nobody had chosen. **BAN is offered only where a ban would hold**: it keys on name + platform, and a merely CLAIMED name is a self-report the peer can change on their next handshake, so `net_identity_anonymous()` (the worker attested the NAME itself — not `NetIdentity::attested()`, which an iOS peer passes on its account alone) gates it and those rows read `KICK` with no arrows. The same predicate powers the trailing **ALLOW ANONYMOUS PLAYERS YES/NO** row, on the roster and in the lobby waiting room alike (`Preferences::allow_anonymous`, saved on change): NO refuses every unattested peer at the two points a handshake first says who answered — the waiting room and the mid-game rejoin door. **Both refuse from INSIDE the handshake**, through one shared verdict (`NetLobby::admit_verdict`, installed on the joining session as `NetSession::set_admit_check` and consulted between the HELLO identity parse and the WELCOME, beside the existing `net_comms_allowed_with` gate). Two things follow from that placement. A refusal reaches the peer as a REJECT reason (`RejectAnonymous`/`RejectBanned`, appended enum values — old builds render their generic refusal text) instead of a bare transport close, which the joiner could only read as "COULD NOT CONNECT / A FIREWALL MAY BE BLOCKING THE GAME" — the game was blaming the player's network for the host's policy. And an attestation still in flight HOLDS the WELCOME (`AdmitWait`, up to `ADMIT_WAIT_MS`, on its own clock ahead of the quiet-peer handshake timeout) instead of losing a race: the worker's verdict is ASYNC and usually lands AFTER the handshake it describes, so judging on arrival refuses attested players for connecting too fast. The waiting room used to carry a grace timer for that and the rejoin door carried none, assuming its immediate re-offer would win the race the first attempt had just lost. Because the REJECT is queued and not yet flushed when the phase turns, both doors hand a refused session to a ~600 ms drain before deleting it (`NetLobby::closing_`, `GLGame::net_closing_` — the same trick the kick's goodbye needs), or the reason dies with the transport. Strict by design: a room of desktop builds attests nobody, so turning it off there closes the room to everyone. Both actions are named on every peer row because nothing used to say a ban existed at all (field, 2026-08-13), and a peer row draws **no footer**: the row is the whole grammar — the arrows say left/right swaps it, `[CONFIRM …]` says press again — so a line underneath spelling out the keys was a third statement of what two labels already said (the offline rows keep their two, since input cycling and press-to-claim have nothing on the row to show them). The row also **skips the name when there isn't one**: it already opens with "PLAYER n" and the unnamed-pilot fallback is that same role label, so an unattested peer read "PLAYER 2   PLAYER 2". The lobby's trailing rows are drawn UNDER the peer rows, so its ladder moves in **drawn** order (peers 0..n-1, then START GAME, the policy row, and BACK TO MENU — `host_row_kind`/`host_row_select` map a visual index to `host_sel_` and back) — stepping the raw index ran the cursor backwards, down off START jumping to the top peer, and the exit BAND was drawn but unreachable, which reads as a broken row. START GAME is labelled as a plain menu option: the cursor says which row confirm answers, so no row spells out a key. Both need **two confirms**, since ENTER is also the start key and the key that opened the screen; any roster change disarms. `GLGame::net_kick_peer`/`NetLobby::host_kick_selected` send `EV_KICKED` (appended, not a PROTO bump — unknown codes are ignored), pump it out, then drop the session (the kicked SEAT is excluded from `net_handshaking_lost_peer()` while that drain runs: lost + parked + a READY session is exactly what a rejoin in flight looks like, so the door adopted the peer on the next tick — unparking the hull just frozen and announcing "PLAYER N RECONNECTED" about the player just removed, and on the ban path tearing the transport down before the goodbye reached the wire): the event is what makes a kick stick, since a peer that only saw its transport close would return through the rejoin door. The client goes terminal via the BYE machinery with its own card ("REMOVED FROM THE GAME"). A kick also **bans** the pilot for the host process's lifetime (`NetLobby::ban_identity`, keyed on case-folded name + platform — a jid is minted per socket, so identity is the only thing that survives a reconnect). Enforced where the handshake first names them: the waiting room refuses to seat a banned Ready, and the mid-game rejoin door drops and re-offers. A NAMELESS peer can be kicked but not banned (nothing to key on; matching on platform alone would bar every desktop player). Hosting a NEW room clears the list — a ban belongs to the room you kicked them out of. Related: a hot-plugged pad only auto-binds to a seat with NO input at all — the pad-reconnect case — never to a keyboard-driven seat, which used to glue a second pilot onto P1. The pause and menu keys stay direct shortcuts, START still toggles pause and an unknown pad's A still joins player 2, so nothing that worked before needs relearning; every pause opens on RESUME. **Online the menu key is not a quit**: mid-game Esc (and pad BACK, and the Android/Xbox `back_pressed`) opens the pause screen instead of exiting — a live room is shared state, so leaving is the deliberate EXIT TO MENU pick, never a reflex key — and with the menu already up Esc backs out of it (resume); at game over the direct exit stands, and offline nothing changes. Touch keeps its pause button plus the EXIT TO MENU tap band and draws no cursor. **Netplay spectator flow**: online, when the local player runs out of lives while the peer plays on (the game only ends when *all* players are out), a 5 s `SPECTATING IN N` countdown runs on the local wreck (`update_spectate()` / `spectate_arming()`), then the camera hands off to the peer (`camera_target()` returns `remote_player()`) and `SPECTATING` shows at the bottom — the viewer keeps their own rotate/fixed camera preference, the peer's is not adopted. The control OSD and the wreck's own GAME OVER indicator are suppressed while spectating; the shared GAME OVER card takes over once the peer is also out. On the client, losing the host while already out is terminal (goes straight to GAME OVER, no rejoin)
- **Intro** (`intro.h/cpp`) — between-level new-object intro screen. `GLGame::maybe_start_intro()` hands the game to a new `Intro` via ownership transfer; the intro (a `friend` of `GLGame`) draws the frozen world's starfield with the new object spinning centre-screen, and on fire/auto-start hands the game back (`request_state_change(game)`), or deletes it (auto-saving) when leaving to the Menu. `StateManager` forwards focus and controller hot-plug events to it alongside the `GLGame` case

### Weapon System

`Weapon::Base` (`weapon/base.h`) defines the interface: `shoot()`, `step()`, ammo tracking. Each weapon has its own `.h`/`.cpp` in `weapon/`:

| File | Weapon | Notes |
|------|--------|-------|
| `weapon/default` | Default gun | Automatic/semi-auto; `level` controls accuracy; `time_between_shots`; burst variants (`burst_count` > 1 in `weapon_configs`) fire a semi-auto N-shot burst per trigger pull at `burst_interval` ms spacing, one ammo per shot — a started burst completes even if the trigger is released, and cancels when the magazine runs dry. Random gun drops (`Ship::random_drop_weapon_index`) exclude the slow semi-auto variants (semi-auto + >100ms re-press) — those rows stay in `weapon_configs[]` for save/netplay `weapon_index` stability but no longer drop |
| `weapon/mine` | Mine | Deployable, limited ammo, large blast |
| `weapon/giga_mine` | Giga Mine | Larger blast than mine |
| `weapon/missile` | Missile | Homing AI; `set_asteroids()` / `set_ship_targets()`; seeks via `query_segment()` |
| `weapon/shield` | Shield | Energy barrier, limited ammo |
| `weapon/god_mode` | God Mode | Timed invincibility (10s); fires periodic shockwaves (150ms); plays special music with a warning phase in the final 3s |
| `weapon/nova` | Nova | Secondary weapon; charges accumulate from asteroid kills (0–9); triggers `ship->nova_detonate()` |
| `weapon/beam` | Pierce Beam | Primary weapon; limited ammo; fires a single fast bolt (`piercing` flag on the `Particle`) that ploughs straight through a line of asteroids instead of stopping at the first, but only continues through asteroids it actually destroys — it stops when it hits one it can't destroy (invincible, or tough not yet broken); one bolt per trigger pull |
| `weapon/lance` | Lance | Primary weapon; limited ammo; one instantaneous full-length pulse per trigger pull (`Lance::RANGE` = the beam bolt's total travel). The weapon just sets `ship->lance_pulse_pending`; `Ship::fire_lance_pulse()` ray-marches it with the grid: kills every killable asteroid along the line — including tough ones, which the lance kills outright — mirror-reflects (carrying remaining distance) off surfaces that reflect bullets (reflective asteroids, armoured faces, phased ghosts), and is blocked by plain invincible asteroids and teleport evades. The traced polyline is kept as a fading `LancePulse` drawn by `GLShip`. Ship/station hits resolve in `GLGame::resolve_lance_ship_hits` (the march only sees asteroids): the firer dies to post-reflection segments only, the partner needs friendly fire on, enemies and the mini-station die (scored like bullet kills), the gen-20 station hull takes multi-hit damage; online a client's polyline is resolved host-side from MSG_LANCE, except enemies, which the client kills instantly and claims with bullet_id 0 (PROTO 20) like bullet claims |
| `weapon/shock` | Shock | Primary weapon (lives in `primary_weapons`, fired via `shoot()`, cycled with the rest); limited ammo; **semi-automatic — one bolt per trigger pull, no hold-to-repeat** (like Beam/Lance; `is_automatic()`==false, `step()` disarms after each shot; `FIRE_INTERVAL` = 200 ms re-press rate limit). Added via `Ship::add_shock`. Spawns a `ShockBolt` (stored in `Ship::shocks`) that grows one staggered segment per tick ahead of the ship, seeks the nearest asteroid/enemy/station/hazard within a **forward 135° cone** (±67.5° of the firing direction) near its advancing tip, and **chains onward only from a *killing* hit**: if a hit fails to destroy the target — a shielded/invincible player, or something that survives more than one shot (tough asteroid, station, comet, pulsar) — the owner calls `ShockBolt::stop()`, which ends the arc there and spawns a **spark burst** at the collision point (drawn in `GLShip::draw_shocks`) instead of arcing past. A one-shot kill (mini-station, seeker, un-shielded enemy, breakable asteroid) leaves it chaining. Then it fades. **Invincible asteroids are never *sought*** (`ShockBolt::seek_target` skips them, so the arc chains to killable targets instead of dead-ending on a rock it can't destroy) **but they still *block* it** — `grow_segment` does a segment-vs-body test and, when a segment runs into an invincible rock, stops the bolt at its surface (like the lance). Asteroid damage/stop is applied in `Ship::collide_grid`; enemy/station/hazard damage/stop in `GLGame` (which owns those lists), reusing the same `kill()`/`hit()` results bullets use so a survivor is detected by the target still being alive. Seek targets: each ship's missile-asteroid list (invincible asteroids seek-excluded but still blocking — see above) plus `shock_targets` (enemies + stations + mini-station + live hazards, refreshed per tick by `GLGame`; other players are added when friendly fire is on). Each bolt carries its `owner` ship and never seeks/hits it, so friendly-fire lightning only arcs to the *other* player (credited like a bullet). Online (PROTO 22): each fired bolt's polyline is replicated both ways (MSG_SHOCK) for the remote view, and the firing side's kills flow through the same claim/authority path as bullets/lance — the client kills asteroids/enemies locally and claims them (bullet_id 0 sentinel), the host applies its own and station/hazard hull damage |
| `weapon/turret` | Turret | Secondary; deploys a `TurretDrone` (defined beside the weapon, stored in `Ship::turrets`) at the ship's tail like a mine — a player-coloured circle with a rotating barrel that fights on its own: seeks the nearest killable target within `RANGE` — re-sought every `SEEK_INTERVAL` (~20 Hz), caching only the computed bearing, never a pointer (asteroids are deleted between ticks) — (asteroids via the owner's missile-asteroid list — invincible rocks and phased ghosts skipped — plus `shock_targets`: enemies/stations/mini-station/hazards, the partner only under friendly fire), turns the barrel toward the **intercept point** — where the target will be when a bullet fired now lands, the constant-velocity lead solved in the turret's frame with the shared `BULLET_SPEED`/`BULLET_TTL_MS` constants, falling back to direct aim when no intercept exists — at `TURN_RATE` (slow idle sweep with no target), and fires only once aligned on that lead for an in-range target. Retires after `LIFETIME_MS` (60 s) or `SHOTS` (30) bullets — whichever first — into a debris burst (no damaging blast); also dies to anything kill-aligned: asteroid contact (`Ship::collide_grid`), hostile bullets / comet-seeker rams / the mini-station's hull (GLGame's turret pass). Unlike mines/missiles it **survives its owner's death and respawn** (`reset()` spares the list; the drone fights on through the countdown) and is swept only at the generation rebuild — silently on both machines, since the client's wipe rides the rollover's QUIET apply. Its bullets are ordinary player bullets minted into the OWNER's `bullets` through `Ship::fire_turret_bullet` — so collision, scoring, kill credit, MSG_SHOT replication and FX_BULLET replay clones all apply unchanged; the mint (and the shot sound, `WorldSound` at the turret) is gated like the gun's (`is_local_player`/`net_remote_gun`), while replicated copies run the same aim/cooldown/ammo bookkeeping so expiry stays in step. Online/replay (PROTO 26, savegame v20): the nx ship-extras record carries a trailing turret section (pos/vel, barrel angle, ms left, cooldown, shots), host-echoed like mines with deploy grace and a vanish debris burst guarded to >1 s of life left (local expiry covers the natural end on every machine); the reader gates the section on the stream's save version so pre-v20 replays still parse |


### Pickup System

All inherit from `Pickup` base class (`pickup.h`). Each pickup implements `draw()` and `apply(Ship*)`. Root-level pairs to each weapon:

| File | Pickup | Effect |
|------|--------|--------|
| `weapon_pickup` | Weapon | Generic gun drop; `weapon_index` selects type |
| `mine_pickup` | Mine | +N mine shots |
| `giga_mine_pickup` | Giga Mine | +N giga mine shots |
| `missile_pickup` | Missile | +N homing missiles |
| `shield_pickup` | Shield | +N shield charges |
| `god_mode_pickup` | God Mode | +10s invincibility |
| `nova_charge_pickup` | Nova Charge | +1 nova charge (auto-drops every 100 asteroid kills) |
| `beam_pickup` | Pierce Beam | +100 beam bolts (violet star) |
| `lance_pickup` | Lance | +100 lance pulses (amber star) |
| `shock_pickup` | Shock | +100 shock bolts (chain-lightning primary; lightning-arc icon) |
| `time_slow_pickup` | Time Slow | Clock icon; on pickup the whole world's wall-clock rate halves for 10 wall seconds (`GLGame::kTimeSlowFactor`/`kTimeSlowWallMs`) while the collector's rotation is compensated (`Ship::time_slow_rotation_comp`) so turning feels unchanged — an aiming window; fire rate/thrust keep their in-game rates (they slow with the world). Implemented as a step-SCHEDULING multiplier (sim steps still advance `step_size` of game time, scheduled `kTimeSlowFactor` further apart), so it composes with the pinned online step rate: the host owns the effect, the countdown + owner index ride every snapshot (savegame v18 scalars, PROTO 24), and the client mirrors the factor in its extrapolation loop so both machines slow in lockstep; replays reproduce it the same way (record wall-spacing carries the slow-mo, the mirrored factor paces the in-between extrapolation). Audio: an engage pitch-dive / release rise (`audio/time_slow_start.wav`/`time_slow_end.wav`, `GLGame::time_slow_cue` — global state cues played unattenuated like the countdown tics, refractory-deduped against the client's countdown-vs-apply boundary race, refills re-cue). Not a cheat (unlike the `-`/`=` time keys). E2e hook: `NEWTONIA_NET_TEST_TIME_SLOW_MS=N` drops one on the living player |
| `turret_pickup` | Turret | +3 deployable sentry drones (teal ring-with-barrel icon) |
| `revive_pickup` | Revive | Co-op only (green cross): revives a fallen partner on their last life; GLGame applies it at the collection site (the pickup can't see the player list). With several players down it revives **the one who has been out longest**, never the collector — `Ship::out_order()` is a monotonic stamp taken in `step()` at the moment a ship is recognised as fully out (dead with no lives), so the smallest stamp wins; an UNSTAMPED ship (0) sorts NEWEST, not oldest — the only way to be unstamped here is to have died in this tick's collision pass, which runs after the step loops and before pickup collection, so treating 0 as oldest let the freshest death jump the queue. It used to take the first fallen in list order, i.e. always the lowest seat however recently they fell |
| `extra_life` | Extra Life | +1 life (heart shape) |

**Drop chances** (per asteroid death, constants in `glgame.cpp`): extra_life 0.3125%, weapon 1.25%, mine 1.25%, giga_mine 0.5%, missile 1.25%, shield 1.25%, god_mode 0.25%, beam 0.375%, lance 0.25%, shock 0.3125%, time_slow 0.25%, turret 0.5%. **Revive** is a separate 10% roll ahead of that table, active only while some player is fully out of lives with a partner still in it, and capped at one in the world at a time; collecting it sets the fallen partner's `lives = 1` and restarts their respawn countdown (`GLGame::revive_fallen_partner`), which online replicates like any respawn and ends the spectator flow by itself.

**Debug cheat** — `NEWTONIA_ALL_WEAPONS=1` grants every primary (all default variants + Beam + Lance + Shock) and every secondary at 999 rounds on spawn and after each respawn (`Ship::give_all_weapons`, re-granted from `GLGame::tick`; a numeric value > 1 sets the rounds instead, e.g. `NEWTONIA_ALL_WEAPONS=30` for quick drain tests). It flags the game as cheated (`Achievements::note_cheat_used()` in both `GLGame` constructors) so no achievements or lifetime stats count, and that flag rides the savegame (`GameState::cheated`) so save/resume can't launder it.

### Asteroid Special Types

Asteroids (`asteroid.h/cpp`) are 9-vertex irregular polygons with per-vertex radius offsets. Special flags:

| Flag | Behaviour |
|------|-----------|
| `invincible` | Cannot be destroyed by bullets |
| `invisible` | Only visible as gravitational-lens distortion (rendered via `WarpPass`) |
| `reflective` | Bounces bullets back |
| `teleporting` | Randomly teleports; has a brief vulnerability window (`teleport_vulnerable`) |
| `quantum` | Collapses to solid when observed by a player |
| `tough` | Requires 5 hits; shows crack lines during damage (`crack_vertex`, `crack_t`, `crack_perp`) |
| `armoured` | One rotating face deflects bullets; `armor_angle` tracks the weak spot |
| `elastic` | Bounces off other elastic asteroids; not set independently — derived from `reflective` in the constructor |
| `phasing` | Cycles between solid and intangible states (`phased`, `phase_timer`) |

Serialization: `capture_state()` / `restore_state()` for save/load.

### Mid-game Hazards

`Hazard` (`hazard.h/cpp`) is a single `Object` subclass whose `kind` selects one of three behaviours (the same flag-selects-behaviour approach as `Asteroid`). Introduced on the mid-game generations that otherwise added nothing new — 9, 11, 12 (displayed levels 10, 12, 13) — with counts scaling by generation. All three are destructible and **must be cleared (along with every killable asteroid) to finish the level** (`tick()` gates the clear countdown on no living hazard remaining); each pays a flat bounty when destroyed but grants no achievements. `GLGame` owns them in a `list<Hazard*> *hazards`, wired exactly like `black_holes`: spawned by `add_hazards()` on each generation rebuild, advanced via `Hazard::update(delta, players)` in the step loop, collided against ships/shots just before the mini-station block, drawn in `draw_objects()`, and serialized in the savegame.

| Kind | Behaviour |
|------|-----------|
| `PULSAR` | Stationary; charges then fires an expanding shockwave ring (`wave_active()` / `wave_radius()`) that *shoves* any ship its front reaches outward (`KNOCKBACK`, an acceleration — never directly lethal, but the push can fling a ship into other hazards). Breaks up after `PULSAR_HEALTH` shots for `PULSAR_REWARD`. Rendered as a neutron star: hot core, accretion ring, rotating lighthouse beams, amber double-ring shockwave |
| `COMET` | A solid-white, asteroid-shaped body that tumbles as it cruises fast in a straight line (wrapping the world) trailing a bright debris tail; lethal to any ship it strikes. Each shot knocks a couple of small killable asteroid chunks loose (`shed_comet_fragment()`); breaks up after `COMET_HEALTH` shots for `COMET_REWARD` |
| `SEEKER` | Homes on the nearest player and rams it (lethal unless invincible); dies to a single shot for `SEEKER_REWARD`. Passes through asteroids like the mini-station; rendered as a blinking red drone |

Serialization: `capture_state()` / `from_state()` (kind, position, velocity, shockwave phase). Destroyed seekers aren't persisted.

### Rendering

**OpenGL compatibility** — `gl_compat.h` and `gles2_compat.h/cpp` abstract desktop OpenGL 3.3 Core vs OpenGL ES 2 (mobile/web/Xbox). `gles2_compat` provides GLSL program wrappers, vertex/color buffers, and line-thickening for platforms where `lineWidth > 1` does nothing (WebGL clamps to 1, macOS core GL ignores it, ANGLE reports a [1,1] range). Real GLES drivers (Android/iOS) rasterize wide aliased lines natively, so `gles2_init` queries `GL_ALIASED_LINE_WIDTH_RANGE` and in-range widths bypass the emulation entirely — the per-draw CPU quad expansion + shared-VBO re-upload was the dominant mobile frame cost (~44 ms/frame in the objects pass at 21 fps, fixed by the bypass). `NEWTONIA_LINE_EMULATION=1` forces the emulation back on for A/B.

**Mesh system** (`mesh.h/cpp`) — GPU geometry with interleaved position/colour data. Builder API:
```cpp
MeshBuilder::begin(mode) → color() → vertex() → end() → Mesh
mesh.upload(); mesh.draw(); mesh.draw_tinted(); mesh.draw_at(); mesh.draw_with_model();
```

**Text** — `Typer` (`typer.h/cpp`): static `draw()`, `draw_centered()`, `draw_lefted()`, `draw_lives()`. Per-character GPU meshes; dynamic window resize.

**WarpPass** (`warp_pass.h/cpp`) — gravitational-lens distortion shader for invisible asteroids. Captures viewport to texture, applies radial-distortion pass. Works on both GLSL 1.50 (desktop) and GLSL ES 1.00 (mobile/web).

**Particles** — `Particle` (`particle.h/cpp`): `Object` subclass with TTL countdown; `GLTrail` (`gltrail.h/cpp`) renders thruster particle trails. A particle can be flagged `kills_invincible` to penetrate shields.

**Split-screen** — `GLGame` renders into multiple viewports for two-player mode. `view/overlay.cpp` handles all HUD elements: score, lives, weapons, temperature, level, respawn timer, keymap, god-mode indicator, touch controls, edge indicators, debug info.

**Starfield** — `GLStarfield` (`glstarfield.h/cpp`): parallax background with layered star densities; star count scales with the `star_density` preference (`star_density_scale()`).

**AsteroidDrawer** (`asteroid_drawer.h/cpp`) — renders asteroids; animates crack lines on tough asteroids.

**BlackHole** (`black_hole.h/cpp`) — stationary hazard; `apply_gravity(Object, delta, gravity_scale)` returns `true` if an object crossed the event horizon; minimap ring rendering.

### Ship Details

`Ship` (`ship.h/cpp`) key features:
- **Heat system**: `temperature`, `max_temperature`, `critical_temperature`, `explode_temperature`, `temperature_ratio()`; thrust/reverse build heat, cooling over time
- **Nova system**: `nova_charge` (0–9), `nova_kill_counter` (0–99), `nova_drops_pending`
- **Invincibility**: `time_left_invincible`
- **Respawn**: `respawn_time`, `time_until_respawn`
- **Weapon management**: `next_weapon()`, `previous_weapon()`, `add_weapon(index)`, `add_mine_ammo()`, `add_giga_mine_ammo()`, `add_missile_ammo()`, `add_shield_ammo()`, `add_god_mode()`, `add_nova_charge(n)`
- **Behaviours**: `add_behaviour()`, `disable_behaviours()`

`Enemy` (`enemy.h/cpp`) — inherits `Ship`; parameterized by `difficulty`; takes target and asteroid lists for the `Follower` behaviour.

`GLShip` (`glship.h/cpp`) — rendering + input wrapper: keyboard, controller (SDL_GameController with analog sticks), touch joystick. Camera following with optional rotation (`toggle_rotate_view`) and configurable smoothing rate (`camera_smoothing` preference; 0 = instant snap).

`GLEnemy` (`glenemy.h/cpp`) — rendering wrapper for `Enemy`.

`GLCar` (`glcar.h/cpp`) — alternative player ship model with left/right jets.

`GLStation` (`glstation.h/cpp`) — inherits `Ship`; rotating ring station with `health` and per-wave difficulty that deploys waves of `GLEnemy` ships; `capture_state()` / `restore_state()` for save/load.

`GLMiniStation` (`gl_mini_station.h/cpp`) — inherits `Ship`; small station that drifts in one random direction, passes through asteroids, and periodically fires at the nearest player (its bullets destroy asteroids but award nothing). Dies to a single player shot for a fixed reward (`REWARD = 1000`, awarded in `GLGame`); `capture_state()` / `restore_state()` for save/load.

### Save / Load

**Savegame** (`savegame.h/cpp`) — binary format, magic "NWTN", version 19:
- `WeaponEntry`: kind, weapon_index, ammo (kinds include the primaries `Beam`, `Lance`, and `Shock` — all captured/restored in the primary-weapon list; `Shock` appended in v15 after the branch's Beam/Lance to keep wire ordinals stable — and the `Turret` secondary, appended in v20; deployed drones themselves are transient like mines, never in the save body)
- `Player`: score, lives, kills, respawning flag, position, velocity, facing, weapons, nova state, achievements bookkeeping (asteroid kills, enemy-ship kills, died-this-generation, weapons-fired mask; appended in v14 together with the game-scoped cheat flag), seat id (1..MAX_PLAYERS, appended in v19 with PROTO 25 — snapshots and the net restore paths key ship records by seat instead of list position; 0 in older files means positional, entry i = seat i+1)
- `Asteroid`: position, velocity, radius, health, all special flags and transient state
- `Pickup`: type, position, weapon_index (the `ShockWeapon` pickup type merged from master, `TimeSlow` appended in v18, `Turret` in v20; new enum values, so older saves still load — they just never contain them)
- `BlackHole`, `Enemy`, `Station`: positional/state data (Station includes its deployed enemies)
- `MiniStation`: present flag, alive, position, drift velocity, rotations, shot timer
- `Hazard`: kind, position, velocity, shockwave phase timer (mid-game pulsar/comet/seeker; merged from master and appended at end of file in v16)
- `GameState`: generation, world size, level_cleared, players, all object lists, game-scoped achievements cheat flag (appended in v11→renumbered v14 on this branch), hazard list (appended in v16), in-flight time-slow effect — sim ms remaining + owning player index (appended in v18), per-player seat ids (appended in v19)

**Backward compatibility:** `GameState::MIN_VERSION..VERSION` all load; older or newer files are ignored. New fields are only ever **appended at the end** and read back gated on `version >= N`, so an older save stops short and the new fields take their defaults (e.g. v9 saves load with no mini-station). Loading then re-saving upgrades the file to the current `VERSION`. Keep this convention when bumping the version so existing saves survive.

**Netplay reuses these structs — update the snapshot rebuild too.** Snapshots serialize through the same `Save::` types, and the restore logic exists in TWO places: the savefile-load switch in the `GLGame(save)` constructor AND the wholesale rebuild in `net_apply_state()` (what a net client applies 10x/s). Anything added to the savefile — a new `PickupType`, `WeaponEntry::Kind`, object list — must be handled in **both**, or the addition silently vanishes on net clients (a missing case is skipped, not an error: the Beam/Lance pickups arrived in client snapshots invisible for exactly this reason; the mid-game hazards merged from master round-tripped through the savegame but were invisible online until `net_apply_state` learned to rebuild them, and the Shock pickup repeated the pattern). **Pickups are now immune**: both paths share the single `make_pickup()` factory in `glgame.cpp` (no `default:` case, so a missed new `PickupType` is a `-Wswitch` warning) — add new pickup types there only. For everything else, grep for the existing enum's cases and extend every switch you find. Objects that MOVE (mini-station, station, comet/seeker hazards) also need a `net_state_sane` bound, a nearest/in-place reconcile in `net_apply_state` (a wholesale delete+recreate teleports them at the 10 Hz apply rate) and an extrapolation step in `tick_net_client` so they glide between snapshots. A world FORCE on the local ship (black-hole pull, pulsar shockwave push) must additionally be re-applied to `players->back()->ship` in `tick_net_client` — the host applies it to its copy, but the client's authoritative pose (PROTO 12) discards that every INPUT, so without the client-side mirror the effect passes straight through the pilot. The resulting collision/death still resolves host-side and replicates. Conversely, anything the CLIENT deploys locally for instant feedback (mines, giga mines, missiles — spawned by `Ship::fire_secondary` while the press is still travelling) is missing from the host's set for the first apply or two, which the wholesale rebuild's vanish detection reads as a host-side detonation: each one blew up at the muzzle and then the host's echo of the SAME projectile flew off ("a double missile where one explodes instantly"). `Ship::NET_DEPLOY_GRACE` stamps every fresh deploy and `nx_hold_unconfirmed` holds an unmatched one for a few applies instead of exploding it — any new locally-predicted deploy needs the same stamp.

Auto-save triggers on pause or player death if the player has lives or score remaining, and on level completion.

**Preferences** (`preferences.h/cpp`) — INI file in SDL pref path; global `g_prefs` instance:
- Per-player (`PlayerKeys`): 12 key bindings (left, right, thrust, shoot, reverse, mine, next_weapon, next_secondary, boost, teleport, help, toggle_rotate_view; defaults WASD + Space/X) plus `keyboard_sensitivity` and `camera_smoothing` floats and a `rotate_view` bool (per-player camera fixed/rotate; the in-game toggle key and the Options screen both write it). Each binding is a two-slot `KeyBinding` (primary + optional alternate; `matches()` is the dispatch test) — P1's directions default to the arrow keys as alternates, P2's carry none. INI format is downgrade-safe (Steam testers switch branches sharing the file): the canonical line stays single-value (`p1_thrust=w`, old builds parse it), the alternate rides a `p1_thrust_alt=up` line old builds ignore (`none` = cleared); a bare value on load replaces the primary and keeps the default alternate, and a comma list (`w,up`) is accepted for hand edits
- General (`GeneralKeys`): pause (P), menu (Esc), add_player2 (Enter), toggle_friendly_fire (G), skip_level (N), toggle_debug_grid (B), time_speed_up (=), time_slow_down (-), time_reset (0), toggle_fullscreen (F)
- Display: `fullscreen` flag, `window_width`/`window_height`
- Camera: legacy global `rotate_view` flag — superseded by the per-player `PlayerKeys::rotate_view`; kept only as a load-time migration seed (old INIs) and a downgrade fallback written from P1
- Gameplay: `friendly_fire` flag, `star_density` multiplier (user-editable float in the INI), `allow_anonymous` (hosting policy — see the seat-roster notes above; default true, so the shipped behaviour is unchanged)
- API: `load_preferences()`, `save_preferences()`; missing keys in old files are silently ignored

**High Score** (`highscore.h`) — `load_high_score()` / `save_high_score(score)`.

### Achievements & Lifetime Stats

Full design in `ACHIEVEMENTS.md` (platform requirements, master list, backend plan).

**Achievements** (`achievements.h/cpp`) — platform-neutral seam: `Achievements::unlock(id)` / `progress(id, pct)` (percent, 100 == unlock) with symbolic string IDs; the default backend is a no-op; platform backends replace it behind their own build flags (Steam: `steam_achievements.cpp` under `STEAM_BUILD`; Game Center: `game_center_achievements.mm` under `GAME_CENTER_BUILD`, defined by the iOS Xcode project and the ios.yml simulator CI, both linking GameKit; `Achievements::init()` is called from `ios_main.mm`; GDK in the private mirror; Play Games: `play_games_achievements.cpp` under `PLAY_GAMES_BUILD`, defined by the root `CMakeLists.txt` for every Android build — a JNI bridge to `android/app/src/main/java/org/newtonia/PlayGamesAchievements.java`, which talks to Play Games Services v2 (automatic sign-in; `unlock` + 100-step incremental `setSteps`; console-generated IDs pasted into `android/app/src/main/res/values/games-ids.xml` under the resource names fixed by the native mapping table — entries stay commented out until the Play Console project exists, and missing entries just drop earns with a logcat warning); `Achievements::init()` is called from `android_main.cpp` after `SDL_Init`, `NewtoniaActivity.onResume()` retries sign-in and flushes the in-memory pre-sign-in earn queue, and offline delivery is Play services' job — this backend never touches the pending journal). Backends whose SDK doesn't queue offline earns (Game Center now, GDK next) record every earn in the shared pending journal (`achievement_journal.h/cpp` → `pending_achievements.dat` in the pref path, keyed by symbolic ID + platform account so profile switches can't cross-credit; single-threaded — marshal SDK completions to the game thread) before attempting delivery, confirm entries out only once the server's achievement list shows them, and resubmit the rest on sign-in/foreground. Local Xcode device builds sign with `ios/EntitlementsDev.plist` (project `CODE_SIGN_ENTITLEMENTS`) so sandbox Game Center works; releases use `ios/Entitlements.plist` injected by deploy-ios.yml. `coop_clear` is mapped on all three backends and defined in all three portals (2026-07-26) — netplay made 2P earnable on touch devices, closing the gap that once left it deliberately unmapped (ACHIEVEMENTS.md §2); Game Center dev reset: `NEWTONIA_RESET_GAME_CENTER=1` via the Xcode scheme. The shared layer owns the XR-057 cheat-suppression flag, which is **game-scoped**: skip-level and time-scale keys call `note_cheat_used()`, `unlock`/`progress` are dropped for the rest of that game, and only a fresh game (`new_game_started()`) clears it — deliberately not per-generation, or skipping to one level short of a progression achievement and clearing a single level would unlock it. The flag rides the savegame (`GameState::cheated`, v11) so save/quit/resume doesn't launder it.

Hooks: `GLGame` (level clear, generation rebuild/progression, station + mini-station destruction, cheat keys) and `Ship` (`credit_asteroid_kill()` shared by every asteroid-kill path, `credit_ship_kill()` on the bullet/missile ship-kill paths, weapon-kind tracking in `shoot()`/`fire_secondary()`/`add_god_mode()`, `nova_detonate()`, death flag in `kill()`). Attribution: only ships with `is_local_player` (set by `GLGame` when creating player ships; false for enemies, stations, and future remote netplay peers) earn achievements and stats.

**Presence, Invites, the universal join link, netplay identity & policy, and the Leaderboard** are documented in `.claude/rules/online-services.md` — auto-loaded when working on `net_*`, `signal/`, `board/`, `invites`/`presence`, platform-identity (`steam_*`, `play_games_*`, `game_center_*`, `ios_*`), or `web/site/` files. Full designs: NETPLAY.md and LEADERBOARD.md.

**Lifetime stats** (`stats.h/cpp`) — standalone roaming `stats.dat` in the SDL pref path (magic "NWST", version 1, append-only format like the savegame): lifetime asteroid kills and a special-type kill mask, deliberately outside `savegame.dat` so netplay still counts. Kill writes are batched (every 10) and flushed via `Stats::flush()` from `save_progress()` and game over. Writes are skipped while the cheat flag is set — `specials_7`/`kills_10000_lifetime` read this file, so cheat games banking lifetime progress would launder achievements. Already in the Steam Auto-Cloud patterns (confirmed in the portal 2026-07-18), so it roams across installs.

### Audio

SDL_mixer; all assets are WAV files in `audio/` (generated by `generate_sounds.py`). Mobile builds copy them into the app bundle/assets. God Mode weapon plays special music with a warning phase in the final 3 seconds. Music tunes (all title-style A-minor synth pieces): menu `title.wav` (8s, `Mix_PlayMusic`), intro screens `intro.wav` (4s loop on its own channel), pause screen `pause.wav` (16s loop on its own channel, silenced while the window is unfocused).

**Distance attenuation — every world sound goes through one of two seams, never a bare `Mix_PlayChannel`.** The listener rule itself lives in one place, `GLGame::world_volume` (`all_players_local()` — offline or a split-screen replay — uses `sound_volume_for_point`, the NEAREST live local player; live online uses `net_listener_volume`, the local camera only, since the peer is somewhere else entirely). **The curve is a plateau, not a cone** (`listener_falloff`): full volume out to `camera_screen_radius` — the half-diagonal of that camera's viewport, so anything that could be ON SCREEN is unattenuated — then a linear fade to silence over another screen-radius past the edge. The fade band is what lets the plateau exist (cutting off at the edge would pop sounds away as they left the view), and the plateau is why the game doesn't sound quieter than it used to: falling off from the camera CENTRE puts an asteroid dying in front of you at half volume. Split-screen audible range is each viewport's own half-diagonal — `camera_screen_radius` divides the window by BOTH viewport counts (it once ignored the horizontal split, which doubled the landscape 2P audible radius; fixed with the 4P grid work, FOURPLAYER.md D5).

- **Sounds a ship plays for itself** (gun, thruster hum, explosion) multiply by `Ship::sound_volume_scale`, which `GLGame` sets per tick: `update_player_sound_volumes()` for players (the local player always 1.0), the enemy/mini-station loops for the rest. The thruster hum is a LOOPING channel, so its level is chunk volume, not a play argument — `Ship::update_boost_volume()` re-levels it and must run per tick, since a held thrust key fires `thrust()` exactly once while the distance keeps moving.
- **A ship's private cues** — the shield-hum and god-mode-music loops, the respawn countdown tics — are gated on `Ship::sound_own_cues`, NOT on a volume. They ask whose ship it is, and `GLGame` sets the flag beside the scale (true for the ships whose screen this is, false for the online peer and for world actors). They used to test `sound_volume_scale >= 1.0f`, which answered that question only because the old curve peaked at literally zero distance; under the plateau anything visible passes it, so don't reintroduce that test.
- **Sounds that belong to a place, not a ship** — the process-wide `Asteroid::explode_sound/thud_sound/ting_sound/asteroid_ting_sound` statics, and the mine/giga-mine/missile/nova detonations (they blow up where the projectile is, not where its owner is) — go through `WorldSound::play(chunk, world_point)` (`world_sound.h/cpp`). `GLGame` installs itself as the `WorldSound` listener in both base constructors and drops it in the destructor (`clear_listener` ignores a stale ctx, since states are constructed before their predecessor is deleted); with no game installed the volume is 1.0, so menus and tests are unaffected. Because these are shared chunks and SDL_mixer applies CHUNK volume at mix time to every channel still playing it, `WorldSound::play` levels a reserved CHANNEL instead (channels 2..9, carved out by the platforms' `Mix_ReserveChannels(2 + POOL)` beside the 0/1 priority pair) — per-chunk leveling retro-leveled instances already ringing. The missile FLY loop is the looping cousin: per-channel volume re-levelled each tick from the missiles' own positions (`Ship::update_missile_fly_volumes`), with the handle's deleter restoring the dynamic channel's volume.

Adding a new world cue: find the position it happens at and call `WorldSound::play` — do not add a fifth hand-rolled `Mix_VolumeChunk` + `Mix_PlayChannel` pair.

## Git workflow

**Always work on a feature branch; never commit or push directly to `master`.**
Every change lands through a pull request. Concretely:

- **No direct commits to `master`, and never force-push `master`.** Master only
  ever changes by merging a PR.
- **Do not self-merge.** An assistant (Claude) opens PRs and gets CI green but
  **merges only with explicit per-PR go-ahead** from a maintainer — a general
  "open a PR" is not merge authority; the human says "merge #NNN" (or does it
  themselves). This applies to squash/rebase/merge alike.
- **Follow-up after a PR merges = a fresh branch + a fresh PR.** A merged PR is
  finished; don't reuse it. Branch anew from the latest `master`
  (`git fetch origin master && git switch -c <new-branch> origin/master`),
  commit the follow-up, open a new PR. **Do not force-push or rewrite the
  history of a branch whose PR already merged** — leave the merged branch
  alone.
- **Prefer not to rewrite shared history.** Force-push only an unmerged branch
  that is solely your own in-flight work, and prefer `--force-with-lease`.

## Scheduled check-ins (Routines / `send_later`)

**Watch a PR until it is GREEN, then stop.** The default end condition for PR
babysitting is *CI green with nothing unanswered* — not "merged or closed".
Once every check on the head commit has passed (skipped counts as passed) and
no review comment is waiting on a reply, the watch is done: say so in one line,
`unsubscribe_pr_activity`, and arm no further check-ins. Waiting on a human to
review or merge is **not** a reason to keep polling — a PR sitting green for
hours produces one identical wake-up after another, each one re-sending the
whole conversation for no new information.

Keep watching past green **only when the human explicitly asks for it** —
"watch it until it merges", "merge it when green" (merge authority implies
waiting for the merge), "tell me when it lands". Absent that, green is the
finish line. This overrides any general PR-babysitting guidance that says a
subscription ends only at merge or close; it does not change the
drive-to-green posture *before* green — a failing check on our own PR is still
ours to fix, and a new review comment still gets addressed or answered.

Babysitting a PR or a deploy usually means arming a `send_later` wake-up. Two
rules, both learned the expensive way (2026-07-25: **53 identical
`delete_trigger` calls over 4.5 hours**, each one interrupted and each retry
re-sending the whole conversation — a very costly no-op).

- **Never retry an interrupted or denied tool call verbatim.** A denial is the
  user declining, not a transient error: adjust the call, pick a different
  approach, or stop and say what's blocked. One retry is defensible if the
  failure looks transient (a network blip); a *second identical* call that
  fails the same way means it will never succeed. This is the general rule —
  it is not specific to Routines, and it is the actual cause of the incident
  above.
- **Never delete a one-shot Routine that has already fired.** `send_later`
  creates a one-shot (`run_once_at`), and firing self-disables it with
  `ended_reason: run_once_fired` — it can never fire again, so there is
  nothing to clean up and no cleanup call to make. Check `ended_reason` via
  `list_triggers` before even considering a delete: if it is set, do nothing.

`delete_trigger` accepts any Routine, but it is only *worth* calling on a
recurring (`cron_expression`) Routine — those never self-disable and deletion
is the only way to stop them permanently — or on a one-shot you want to cancel
**before** it fires. Note the minimum cron interval is hourly, so a recurring
Routine can't wake faster than that. To fix a bad cron or a wrong prompt, or to
pause something reversibly, prefer `update_trigger` (`enabled: false`, or edit
the fields in place): it keeps the Routine's ID and run history, which
delete-and-recreate throws away.

## CI/CD

GitHub Actions runs builds on every push to `master`/`main` and on PRs (feature branches build once via their PR, not twice):

| Workflow | Output |
|----------|--------|
| `.github/workflows/macos-dev.yml` | Universal arm64+x86_64 binary |
| `.github/workflows/android.yml` | Debug APK + the emulator TLS gate (api-30 x86_64 AVD runs `NEWTONIA_SIGNAL_SELFTEST` via `android/emulator_selftest.sh`) |
| `.github/workflows/ios.yml` | iOS simulator build |
| `.github/workflows/linux.yml` | Linux executable (netplay + headless loopback self-test) |
| `.github/workflows/windows.yml` | Windows executable (netplay: MinGW-static libdatachannel + self-test — the compile gate for deploy-steam's Windows build) |
| `.github/workflows/web.yml` | WebAssembly + GitHub Pages deploy (master/main only) |
| `.github/workflows/e2e.yml` | The headless e2e suite (TESTING.md §4) + both worker suites: one build job, then six shards (`test/e2e/ci_shard.sh`, which also runs locally) each booting their own local relay. ~15 min wall for ~58 min of serial driver time; drivers run one at a time inside a shard, since concurrent netplay drivers break each other's timing assertions. `leaderboard.sh` is the one exception — its own shard, master pushes only, no retry (measured 524-900+ s on identical code) |
| `.github/workflows/xbox-dev.yml` | GDK Desktop (Gaming.Desktop.x64) build — catches Xbox-port compile errors without hardware. One of the two canaries this repo keeps green now that Xbox work is deferred to the private repo |
| `.github/workflows/xbox-console-smoke.yml` | Compile-only check of the `_GAMING_XBOX` console paths with MSVC under `WINAPI_FAMILY_GAMES` (no GDKX/NDA material; GDK-only headers stubbed in `xbox/smoke_stubs/`) |

**Deployment workflows** (triggered by `v*.*.*` version tags or manual dispatch; the old `netplay-v*` test namespace was retired post-launch — historical `netplay-v*` tags remain in the repo but trigger nothing):
- `.github/workflows/deploy-steam.yml` — Steam (Windows/macOS/Linux via Steamworks SDK); tags and the manual-dispatch default both go to the `beta` branch
- `.github/workflows/deploy-ios.yml` — TestFlight
- `.github/workflows/deploy-android.yml` — Play Store
- `.github/workflows/deploy-itch.yml` — Itch.io (pushes only the playable game `web/dist/play`, not the landing page)
- `.github/workflows/deploy-board.yml` — the leaderboard Worker (`board/`): same tag→production / master-push→beta scheme as deploy-signal (beta = `newtonia-board-beta`, its own D1/R2; `NEWTONIA_BOARD_URL=wss://newtonia-board-beta.gfmcc.workers.dev/board`), also redeploys beta when the shared `signal/src/*_verify.js` modules change; gated on the board unit + `wrangler dev --local` protocol tests. Same repo secrets as deploy-signal; one-time D1/R2 creation runbook in `board/README.md`
- `.github/workflows/deploy-signal.yml` — the Cloudflare signaling Worker (`signal/`): `v*.*.*` tags deploy production (`newtonia-signal`, a plain `wrangler deploy` — the top-level config, so runtime secrets/DO state carry over); pushes to master touching `signal/**` auto-deploy the isolated beta worker (`newtonia-signal-beta`, wrangler.toml's `[env.beta]` — own DO namespaces and secrets; point a build at it with `NEWTONIA_SIGNAL_URL=wss://newtonia-signal-beta.gfmcc.workers.dev/ws`); manual dispatch picks either (default beta). Both gated on the signal unit tests + the `wrangler dev --local` protocol tests. Needs repo secrets `CLOUDFLARE_API_TOKEN` + `CLOUDFLARE_ACCOUNT_ID`

All deploy artifacts build with netplay (NETPLAY.md M3-5): web/Android have it inherently (Emscripten backend is unconditional; root CMakeLists defaults `NEWTONIA_NET=ON`), deploy-ios feeds the device libdatachannel build through the pbxproj's `NEWTONIA_NET_DEFINE`/`NEWTONIA_NET_HEADER_PATH` vars, and deploy-steam builds libdatachannel per platform. Each native deploy job runs the headless `NEWTONIA_NET_SELFTEST` loopback as a gate; the dev workflows above prove the same recipes on every push.

**Disabled workflows** — `.github/workflows/disabled/` holds inactive workflows (`deploy-macos.yml`, `deploy-windows.yml`, `deploy-xbox.yml`, and `video.yml` — the replay-to-MP4 renderer, parked because its artifacts are ~240 MB against a 2 GB quota shared with every other workflow here); move a file back into `workflows/` to re-enable it.

**Steam integration** — `steam_build.h` (constants/SDK), `steam_achievements.cpp` (achievements backend behind `STEAM_BUILD`; symbolic→`ACH_*` mapping, progress via increment-only pct stats), `steam_presence.cpp` (rich-presence backend behind `STEAM_BUILD`), `steam/` contains Steamworks VDF config files (`app_build.vdf`, `depot_build_windows.vdf`, `depot_build_macos.vdf`, `depot_build_linux.vdf`, plus `rich_presence.vdf` — pasted manually into the portal, not uploaded by a workflow).

## Conventions & Patterns

1. **GL-prefix pattern** — Rendering wrappers (`GLShip`, `GLGame`, `GLEnemy`, `GLCar`) wrap pure logic classes. Never put rendering into logic classes. (`GLStation` and `GLMiniStation` are the existing exceptions — they inherit `Ship` directly.)
2. **Weapon/pickup pairs** — Every weapon type has a corresponding `*_pickup` class at root level.
3. **Behaviour pattern** — Abstract `Behaviour` base with `done` flag; `Ship` owns a list and runs them each frame.
4. **State machine** — `StateManager` + `State` subclasses drive all top-level transitions.
5. **Serialization** — Major game objects implement `capture_state()` / `restore_state()` pairs.
6. **Grid collision** — Use `Grid` for all spatial queries; never iterate all objects.
7. **Mesh builder** — All geometry is pre-uploaded to GPU VBOs via `MeshBuilder`; no immediate-mode GL calls.
8. **Platform abstraction** — Use `gl_compat.h` macros; never call desktop-only GL functions directly.
9. **File naming** — Behaviours: `*_behaviour.h/cpp`. Weapons: `weapon/*.h/cpp`. Pickups: `*_pickup.h/cpp` at root. Views/HUD: `view/*.h/cpp`.
10. **C++11** — Codebase targets C++11 (`-std=c++11`). Avoid later standard features.
11. **The iOS Xcode project is generated by XcodeGen — never hand-edit the pbxproj.** `ios/Newtonia-iOS.xcodeproj/` is gitignored and rebuilt from `ios/project.yml` (`brew install xcodegen && cd ios && xcodegen generate`); both `ios.yml` and `deploy-ios.yml` run `xcodegen generate` before they build through the project. The spec **globs** the same source set the Makefile compiles — root `*.cpp` + `weapon/*.cpp` + `view/*.cpp` (minus the other platforms' entry points: `glut.cpp`, `android_main.cpp`, `web_main.cpp`, `xbox_main.cpp`, `play_games_achievements.cpp`) plus the three Objective-C++ files (`ios_main.mm`, `ios_share.mm`, `game_center_achievements.mm`). So a **new `.cpp` at root / `weapon/` / `view/` needs no project edit** — it's picked up automatically, and master merges can no longer collide object IDs (the class of bug that silently dropped `net_transport.cpp` from the compile). Only touch `ios/project.yml` when adding a **new source directory**, a framework, or a build setting; a new platform-specific entry point that must NOT compile on iOS goes in that spec's `excludes:` list. This replaced a hand-maintained pbxproj where every file had to be added to four sections under a globally-unique `AA…`/`AB…` ID.
