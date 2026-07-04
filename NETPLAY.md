# Online 2-Player Co-op — Implementation Worklog

Working doc for the netplay effort. The full approved plan is reproduced in the appendix at the bottom of this file; the sections here are the operational summary.

## Decisions (locked with Glenn)

- **Transport**: WebRTC DataChannels. Native = **libdatachannel C API** (`rtc/rtc.h`; lib builds C++17 internally, game stays C++11). Web = browser RTCPeerConnection via EM_JS in `net_transport_web.cpp`.
- **Topology**: P2P host-authoritative. Host sims everything; client sends INPUT (unreliable channel), receives chunked snapshots (reliable channel, 10 Hz, 15 KB chunks) = savegame serialization + NetExtras (ship transients, projectiles, asteroid net_id array).
- **Signaling M1**: manual clipboard copy-paste of base64 SDP (non-trickle). No server, no PlayFab, no TURN. Room codes/signaling service = M2.
- **M1 platforms**: Windows (xbox/CMakeLists.txt GDK Desktop) + web. Other platforms compile the seam disabled (`NEWTONIA_NET_RTC` defined only by xbox CMake; web keys off `__EMSCRIPTEN__`; console smoke/Android/iOS get empty TUs).
- **Client sync**: snapshot-driven + local extrapolation (no lockstep — `rand()` is host-only authority). Asteroids get `uint32_t net_id` for cross-snapshot matching; local ship lerp-corrected (~0.35/snapshot); pickups rebuilt per snapshot.
- **Save protection**: all save paths (`save_progress`, death save, game-over delete) hard-gated off when `net_mode_ != Off`. Intro states suppressed online (2 s banner instead). `focus_lost` doesn't auto-pause online.
- **Pins**: MbedTLS `v3.6.6`, libdatachannel `v0.24.5` (verified to exist via ls-remote 2026-07-04).

## Phase status

- [ ] **Phase 0 — spike**: prove FetchContent MbedTLS+libdatachannel, USE_MBEDTLS=ON, NO_MEDIA/NO_WEBSOCKET/NO_EXAMPLES/NO_TESTS, /MT static CRT, MSVC; link + run a C-API loopback (also validates `rtcDataChannelInit`/reliability struct). **Spike committed (`spike/netplay/`) + runs in CI (`netplay-spike.yml`, windows-latest, vcvars64+Ninja, executes the loopback + rejects dynamic-CRT imports); iterating on FetchContent interop findings** (see session log).
- [x] **Phase 1 — seam**: `net_transport.h/.cpp` (interface + factory→nullptr + `net_available()`), `net_transport_rtc.cpp` (all inside `#ifdef NEWTONIA_NET_RTC`), `net_transport_web.cpp` (all inside `#ifdef __EMSCRIPTEN__`), `net_protocol.h`. No behavior change; every build globs them fine. **Landed 955e357; all platform workflows green** (web.yml only runs on master/main — web stub syntax-checked locally with `-D__EMSCRIPTEN__`).
- [ ] **Phase 2 — native backend**: NEWTONIA_NET option block in xbox/CMakeLists.txt (FetchContent per pins above, link `LibDataChannel::LibDataChannelStatic`); fill rtc backend (2 channels "rel"/"unrel", mutex inbound deque, atomics, non-trickle: expose SDP only at RTC_GATHERING_COMPLETE); `NEWTONIA_NET_SELFTEST=1` env → in-process loopback PASS/FAIL log in xbox_main.cpp.
- [ ] **Phase 3 — web backend**: EM_JS `Module.__nwnet` (pc, rel, unrel, inbox[], state, localDesc), poll-style; `navigator.clipboard` helpers (gesture-gated read).
- [x] **Phase 4 — savegame streams**: `Save::Stream`/`FileStream`/`MemStream`; `wv/rv/wa/ra` + per-type helpers `FILE*`→`Stream&`; `serialize_game`/`deserialize_game` (no magic/version — save_game/load_game wrap; deserialize takes the file's version for the v10 mini-station gate). **Landed 510cf0b; byte-identical verified** (synthetic GameState covering every format branch → identical sha256 before/after; MemStream serialize→deserialize→serialize reproduces the file body exactly); all platform workflows green.
- [ ] **Phase 5 — lobby + handshake**: `net_session.h/cpp` (HELLO/WELCOME/REJECT, chunk/reassemble, base64), `net_lobby.h/cpp` State (HOST/JOIN clipboard flow), ONLINE row in menu.cpp behind `net_available()`. Exit: two instances CONNECTED.
- [ ] **Phase 6 — host side**: GLGame net ctor + `net_mode_`, `add_remote_player()` (add_player2 minus bindings), `apply_remote_input()` (held bitmask + wrapping one-shot counters + respawn tap per glship.cpp:513), snapshot build/send @10 Hz, gating (saves/intro/pause/focus/add_player2/debug keys), single viewport online, asteroid `net_id`.
- [ ] **Phase 7 — client side (the milestone)**: `tick_net_client()` (visual/kinematic stepping only), `apply_snapshot()` (net-id map, death debris for missing ids, generation rebuild via rollover-block clone, local-ship blend), INPUT send per tick, client bootstrap via existing `GLGame(const Save::GameState&,…)` from snapshot #1.
- [ ] **Phase 8 — polish**: pause propagation, disconnect UX ("CONNECTION LOST"), generation banner, 1 s input dead-man switch, REJECT UX, native↔web cross-play checklist.

## Protocol quick-ref

Header: `uint8 proto_ver(=1) | uint8 msg_type | uint8 player_id | uint8 reserved`, little-endian, explicit byte packing.
Types: HELLO(1) C→H rel; WELCOME(2)/REJECT(3) H→C rel; INPUT(4) C→H unrel (uint32 seq, uint16 held bitmask, uint8 wrap-counters: boost/next_weapon/next_secondary/teleport/respawn_tap, 3 floats analog); SNAPSHOT_CHUNK(5) H→C rel (uint32 snap_id, uint16 idx, uint16 count, bytes); EVENT(6) rel (PAUSE/RESUME/GENERATION_START+gen/GAME_OVER/BYE).

## Verification checklist (M1 done =)

Two newtonia.exe on one machine: paste-connect, both ships controllable, remote one-shots work, host kills explode on client, pickups reflect, pause syncs, generation rollover on both, kill-process → CONNECTION LOST → Menu, solo save intact afterward. Then native↔web (Chrome+Firefox clipboard, chunking). CI: all three workflows green each phase.

## Session log

- **2026-07-04 (b)**: Phases 0-commit/1/4 this session (remote Linux container; Windows spike runs via CI instead of Glenn's box).
  - **Phase 0**: spike committed as `spike/netplay/` (`main.c` is deliberately C, not C++ — the Android CMake `GLOB_RECURSE *.cpp` would otherwise suck it into the game build) + `netplay-spike.yml` (windows-latest, vcvars64+Ninja like xbox-dev.yml; builds, checks `dumpbin /dependents` for dynamic-CRT imports, runs the loopback, requires exit 0). Two FetchContent interop findings so far, both now fixed in the spike CMake and **both needed again in Phase 2's xbox/CMakeLists.txt block**:
    1. libdatachannel's `FindMbedTLS.cmake` only searches installed distributions — but its CMakeLists skips `find_package` when `MbedTLS::MbedTLS` already exists, so alias `mbedtls`/`mbedx509`/`mbedcrypto` → `MbedTLS::MbedTLS`/`::MbedX509`/`::MbedCrypto` after the MbedTLS FetchContent.
    2. Its `install(EXPORT LibDataChannelTargets)` fails at generate because the in-tree mbedtls target is in no export set → `set(CMAKE_SKIP_INSTALL_RULES ON)` (nothing is installed anyway).
    Good news from run 1: the v0.24.5 pin's submodules (libjuice/usrsctp/plog) fetch fine via FetchContent.
  - **Phase 1**: seam landed (955e357), no behavior change, all platform workflows green. `net_protocol.h` carries the header/enums + LE byte-packing helpers and a bounds-checked `Net::Reader`; message-specific encode waits for Phase 5. Note web.yml only triggers on master/main, so branch CI doesn't cover the Emscripten glob — web stub was syntax-checked locally with `-D__EMSCRIPTEN__`.
  - **Phase 4**: Stream refactor landed (510cf0b) with byte-identical proof: synthetic GameState exercising every format branch (all asteroid specials incl. tough cracks, station+enemies, mini-station, all pickup types, black hole, 2 players, god-mode weapon entry) → sha256-identical `savegame.dat` before/after refactor, plus MemStream serialize→deserialize→serialize == file body. Harness lives in the session scratchpad only; re-create from this description if needed.
- **2026-07-04**: Plan approved. Pins verified via `git ls-remote` (MbedTLS v3.6.6, libdatachannel v0.24.5). Phase 0 spike written: a scratch CMake project + C-API loopback `main.c` exercising `rtcCreateDataChannelEx` with `reliability.unordered/unreliable/maxRetransmits=0` — the exact struct the real backend needs. CMakeLists = FetchContent MbedTLS v3.6.6 (`ENABLE_PROGRAMS/TESTING OFF`) + libdatachannel v0.24.5 (`USE_MBEDTLS/NO_MEDIA/NO_WEBSOCKET/NO_EXAMPLES/NO_TESTS ON`), `CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"`, link `LibDataChannel::LibDataChannelStatic`; main.c = in-process two-PC loopback (desc/candidate callbacks cross-wired, rel + unrel channels, binary echo round-trip, prints SPIKE PASS/FAIL). **Spike not yet run**: on Glenn's Windows box `cmake` is not on PATH; VS 2022 **Build Tools** live at `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools` (found via vswhere) — run the spike from a `vcvars64.bat` environment (bundled cmake/ninja), same as `.github/workflows/xbox.yml` does.

---

# Appendix: full approved plan (Milestone 1)

## Context

Newtonia currently has local-only 2-player (split-screen, one machine) and zero networking code. Goal: **playable online 2-player co-op** with full cross-platform reach as the end-state. Decisions made with the user:

- **Transport**: WebRTC DataChannels — the only UDP-like transport a browser supports. Native (Windows) uses **libdatachannel** via its **C API** (`rtc/rtc.h`) so the game stays C++11 (libdatachannel compiles C++17 in its own static lib); web uses the browser's `RTCPeerConnection` via Emscripten interop.
- **Topology**: P2P, **host-authoritative** — host simulates everything; client sends input and renders snapshot-corrected extrapolation.
- **No PlayFab, no Steam networking, no signaling server yet**: Milestone 1 signaling is manual clipboard copy-paste of base64 SDP offer/answer. Room codes + hosted signaling are a later milestone (signaling sits behind a small interface for the swap).
- **Players**: 2 now; protocol carries `player_id` + version byte so N-player is possible later.
- **Milestone-1 platforms**: Windows native (`xbox/CMakeLists.txt` GDK Desktop — the active CI target) + Web (`make web`). Linux/macOS Makefile netplay is a documented stretch; Xbox console / Android / iOS get a compiled-but-disabled seam so all CI stays green.

## New files (picked up automatically by every build's source glob)

| File | Purpose | Gate |
|---|---|---|
| `net_transport.h/.cpp` | abstract transport + `NetTransport::create()` factory (returns `nullptr` where unavailable); `net_available()` inline | none — compiles everywhere |
| `net_transport_rtc.cpp` | native libdatachannel C-API backend | whole file `#ifdef NEWTONIA_NET_RTC` |
| `net_transport_web.cpp` | Emscripten backend: EM_JS/EM_ASM inline JS (`Module.__nwnet` with pc/channels/inbox, poll-style like the existing `web_on_idb_ready` pattern) + async clipboard helpers | whole file `#ifdef __EMSCRIPTEN__` |
| `net_protocol.h` | message enums, packed encode/decode (explicit byte packing, never memcpy structs) | none |
| `net_session.h/.cpp` | handshake, snapshot chunking/reassembly, input encode, base64 | none |
| `net_lobby.h/.cpp` | new `State`: HOST/JOIN flow, clipboard signaling UX, status text via `Typer::draw_centered`, starfield background | none (menu hides ONLINE when `net_available()` is false) |

`NEWTONIA_NET_RTC` is defined only by `xbox/CMakeLists.txt` (new `NEWTONIA_NET` option, ON for GDK Desktop, forced OFF for `XBOX_SCARLETT`/console). xbox-console-smoke.yml compiles the new root files as near-empty TUs (no define, no link step) — green by construction.

### Transport interface (net_transport.h)

`start_host()`, `start_join(offer)`, `local_description_ready()` (gathering complete — non-trickle, all ICE candidates embedded in the pasted SDP), `local_description()`, `set_remote_answer()`, `connected()`, `failed()`, `send_reliable()`, `send_unreliable()`, `poll(out)` (main-thread pop), `close()`.

Native impl: one peer connection (STUN `stun:stun.l.google.com:19302`), two channels — `"rel"` (ordered/reliable: handshake, snapshots, events) and `"unrel"` (`unordered, maxRetransmits=0`: input). libdatachannel callbacks fire on worker threads → mutex-guarded inbound deque + atomic state flags; callbacks only enqueue. `<mutex>`/`<atomic>` are C++11.

## Build integration (xbox/CMakeLists.txt)

FetchContent **MbedTLS v3.6.6** (avoids OpenSSL-on-CI pain) then **libdatachannel v0.24.5** (`USE_MBEDTLS=ON, NO_MEDIA, NO_WEBSOCKET, NO_EXAMPLES, NO_TESTS`), link `LibDataChannel::LibDataChannelStatic`. /MT is inherited via the existing global `CMAKE_MSVC_RUNTIME_LIBRARY` (CMP0091 NEW, lines 7/118). libdatachannel vendors libjuice/usrsctp/plog as git submodules — test that the pin fetches them (avoid GIT_SHALLOW if it breaks submodules). **No workflow file edits needed** (xbox.yml's configure-retry loop already covers fetch flakes). Web needs no Makefile changes (EM_JS lives in a normal .cpp).

## Protocol (little-endian; 4-byte header: proto_version, msg_type, player_id, reserved)

- `HELLO` (C→H, rel): proto version + `Save::GameState::VERSION` + build hash → `WELCOME` (player_id, step_size, snapshot period) or `REJECT`.
- `INPUT` (C→H, **unrel**, every 8 ms tick): uint32 seq; uint16 held-button bitmask (left/right/thrust/reverse/shoot/secondary); wrapping uint8 counters for one-shots (boost, next_weapon, next_secondary, teleport, respawn-tap) so lost packets can't drop or double-fire them; 3 analog floats. Host ignores stale seq; **dead-man's switch** zeroes held state if input stops >1 s.
- `SNAPSHOT_CHUNK` (H→C, rel, 10 Hz): snap_id + chunk idx/count + bytes; chunks of **15 KB** (safe under browser DataChannel interop limits). Payload = savegame serialization + `NetExtras` (per-ship transients the save format omits: alive/temperature/respawn/invincibility/god-mode/shield + the public projectile vectors `bullets/missiles/mines/giga_mines/shockwaves` (ship.h:99–101) + asteroid `net_id` array). ~20 KB × 10 Hz ≈ 1.6 Mbps — fine for M1; delta snapshots deferred.
- `EVENT` (rel, both ways): PAUSE / RESUME / GENERATION_START(+gen) / GAME_OVER / BYE.

## Savegame reuse (savegame.h/cpp — contained mechanical refactor)

Add `Save::Stream` (virtual write/read), `FileStream` (fwrite/fread) and `MemStream` (vector-backed); change `wv/rv/wa/ra` (savegame.cpp:24–34) and every per-type helper from `FILE*` to `Stream&`; expose `serialize_game`/`deserialize_game` (no magic/version — `save_game`/`load_game` keep wrapping them). **No disk-format change**; verify byte-identical saves before/after (`fc /b`). `net_id` travels in NetExtras, NOT in the save record — the append-only savegame convention is untouched, no VERSION bump.

## Sync model (the core design decision)

**Snapshot-driven client with local extrapolation** (not deterministic lockstep — `rand()` is seeded per-machine and used everywhere; only the host rolls dice).

- **Host**: normal sim. Start of `tick()`: poll net, `apply_remote_input()` to player 2; end: every 100 ms, `build_save_data()` (exists, glgame.cpp:352) + NetExtras → serialize → chunk → send.
- **Client bootstrap**: NetLobby waits for snapshot #1, then uses the **existing** `GLGame(const Save::GameState&, …)` constructor (glgame.cpp:198) — full world reconstruction for free.
- **Client per-frame**: new `tick_net_client()` runs the same 8 ms accumulator but only visual/kinematic stepping (asteroid/pickup/projectile motion, particles, trails) — **no** collide/drops/kills/generation logic. Local ship predicts from local input; every tick an INPUT message is sent.
- **`apply_snapshot()`** (new GLGame method): players' stats overwrite; remote ship pose snaps; **local ship blends** (`lerp` factor ~0.35/snapshot); projectiles replaced from NetExtras. **Asteroids get a `uint32_t net_id`** (asteroid.h/cpp, host-assigned counter) — client keeps an id→Asteroid map: match → overwrite pose/health/transient flags (all already in `Save::Asteroid`); new → construct+`restore_state` (existing path); missing → play the existing death visual (debris to `dead_objects`, mirroring glgame.cpp:884–890) so kills still explode. Pickups rebuilt each snapshot; station/mini-station/enemies via existing `restore_state`. Generation change (world size differs) → replicate the rollover block (glgame.cpp:616–658): new Grid/boundaries/starfield, then apply.
- **Rendering online**: `num_x/y_viewports()` return 1; each machine draws one full view following its local player (host = `players->front()`, client = `players->back()`).

## GLGame gating (all keyed on new `net_mode_ != Off`)

- **Save protection**: `save_progress()` (called from destructor glgame.cpp:136, back_pressed, focus_lost, death path glgame.cpp:1186) and game-over `delete_save()` become no-ops online — otherwise online play clobbers the local solo save.
- `maybe_start_intro()` returns immediately online; host sends GENERATION_START and both sides show a 2 s banner (name table at glgame.cpp:452–471) instead of the Intro state.
- `toggle_pause()` sends PAUSE/RESUME; receiving one toggles without re-sending. `focus_lost()` does NOT auto-pause online (mutes only).
- `add_player2` key/controller paths disabled online; new `add_remote_player()` (clone of glgame.cpp:557 minus controller/key bindings) creates the remote GLCar. `apply_remote_input()` drives the public Ship setters (ship.h:55–61) + one-shot counters; respawn-tap mirrors glship.cpp:513–517 semantics (friend access via ship.h:185).
- Debug/host-only keys (friendly-fire G, skip-level N, time scale) ignored on the client.

## Menu / lobby UX

- `menu.cpp`: ONLINE row after NEW GAME when `net_available()` — extend `max_menu_items()` (menu.cpp:661), draw block (~230–256), `confirm_selection()` (menu.cpp:706) → `new NetLobby()`. Optionally behind `is_beta_feature_enabled()` until phase 7 lands.
- NetLobby: CHOOSE → HOST ("offer copied to clipboard — send to friend; press V to paste answer") / JOIN ("press V to paste offer" → "answer copied — send back"). Base64 SDP + 1-byte offer/answer/magic prefix (catches wrong-direction pastes). Native clipboard via SDL; web via `navigator.clipboard` inside the V-key gesture (async poll, "clipboard blocked" fallback). On connect: HELLO/WELCOME, host starts `GLGame(session, Host, ctrl)`, client waits for snapshot #1. `failed()` → message → back to CHOOSE. In-game disconnect/BYE → "CONNECTION LOST" → Menu.

## Explicit Milestone-1 scope cuts

Fresh game only (no online save/resume); Intro states suppressed online (banner instead); no rejoin; STUN-only (no TURN — strict-NAT pairs get a clear failure message after ~20 s); host settings win; keyboard/controller lobby only; some remote one-shot sounds missed at 10 Hz; local high score only; exactly 2 players.

## Implementation phases (each lands with all CI green)

0. **Spike**: prove FetchContent MbedTLS + libdatachannel `USE_MBEDTLS` builds /MT under MSVC Ninja (the riskiest external); confirm pin + submodule fetch + C-API reliability struct. Adjust pins.
1. **Seam**: all `net_*.h/cpp` as no-op stubs (factory → nullptr). Zero behavior change everywhere.
2. **Native backend**: CMake block + fill `net_transport_rtc.cpp`; hidden `NEWTONIA_NET_SELFTEST=1` in-process offer/answer loopback logging PASS/FAIL.
3. **Web backend**: fill `net_transport_web.cpp` (EM_JS + clipboard); test with two browser tabs.
4. **Savegame Stream refactor**: byte-identical verification.
5. **NetLobby + Menu + handshake**: two machines reach CONNECTED + HELLO/WELCOME.
6. **Host side**: net ctor, `add_remote_player`, `apply_remote_input`, snapshot capture/send, save/pause/intro gating, single viewport, asteroid `net_id`.
7. **Client side (the milestone)**: `tick_net_client`, `apply_snapshot`, net-id map, generation rebuild, local-ship blend, input send.
8. **Polish**: pause propagation, disconnect UX, generation banner, dead-man timeout, cross-play hardening.

## Verification

1. **Two `newtonia.exe` instances on one Windows machine** (ICE works on loopback/LAN): connect via copy-paste; both ships controllable; remote shoots/boosts/teleports/cycles weapons; host-side asteroid kill explodes on client; pickups reflect; pause from either side pauses both; level clear → banner → new world on both; kill one process → other shows CONNECTION LOST → Menu; **pre-existing solo save still loads afterward**.
2. **Native↔web cross-play**: `make web`, serve `web/dist`, browser joins native host and vice-versa; clipboard UX in Chrome + Firefox; 15 KB chunking against browser channels.
3. **CI**: xbox.yml/windows-glon12.yml build with the new FetchContent deps; console-smoke green by construction (no `NEWTONIA_NET_RTC` → empty TUs). Phase-2 loopback self-test is the automatable net check. Linux Xvfb workflow only regression-tests that the menu still works (no net backend there in M1).
4. Explicitly test tough/armoured/phasing/quantum levels online (snapshot apply must carry their transient state — fields verified present in `Save::Asteroid`).

## Key risks

- **MbedTLS/libdatachannel FetchContent interop** — de-risked by Phase 0 before any product code.
- **Snapshot bandwidth late-game** — measured in Phase 6; fallback: 5 Hz above a size threshold; delta snapshots = Milestone 2.
- **Web clipboard permissions** — reads only inside a key gesture; visible fallback message.
- **Solo-save corruption** — hard gate on `net_mode_`, covered in verification.

## Later milestones (out of scope, direction only)

M2: room codes + tiny hosted signaling (Cloudflare Worker or VPS — interface already in place), TURN fallback, delta snapshots. M3: Xbox/Android/iOS enablement (libdatachannel builds for all three; iOS xcodeproj needs manual file adds), touch lobby UX.
