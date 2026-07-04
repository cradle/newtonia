# Online 2-Player Co-op — Implementation Worklog

Working doc for the netplay effort. Full approved plan: `C:\Users\glenn.GLENNPC\.claude\plans\soft-swimming-nova.md` (key points reproduced here so this file stands alone).

## Decisions (locked with Glenn)

- **Transport**: WebRTC DataChannels. Native = **libdatachannel C API** (`rtc/rtc.h`; lib builds C++17 internally, game stays C++11). Web = browser RTCPeerConnection via EM_JS in `net_transport_web.cpp`.
- **Topology**: P2P host-authoritative. Host sims everything; client sends INPUT (unreliable channel), receives chunked snapshots (reliable channel, 10 Hz, 15 KB chunks) = savegame serialization + NetExtras (ship transients, projectiles, asteroid net_id array).
- **Signaling M1**: manual clipboard copy-paste of base64 SDP (non-trickle). No server, no PlayFab, no TURN. Room codes/signaling service = M2.
- **M1 platforms**: Windows (xbox/CMakeLists.txt GDK Desktop) + web. Other platforms compile the seam disabled (`NEWTONIA_NET_RTC` defined only by xbox CMake; web keys off `__EMSCRIPTEN__`; console smoke/Android/iOS get empty TUs).
- **Client sync**: snapshot-driven + local extrapolation (no lockstep — `rand()` is host-only authority). Asteroids get `uint32_t net_id` for cross-snapshot matching; local ship lerp-corrected (~0.35/snapshot); pickups rebuilt per snapshot.
- **Save protection**: all save paths (`save_progress`, death save, game-over delete) hard-gated off when `net_mode_ != Off`. Intro states suppressed online (2 s banner instead). `focus_lost` doesn't auto-pause online.
- **Pins**: MbedTLS `v3.6.6`, libdatachannel `v0.24.5` (verified to exist via ls-remote 2026-07-04).

## Phase status

- [ ] **Phase 0 — spike**: prove FetchContent MbedTLS+libdatachannel, USE_MBEDTLS=ON, NO_MEDIA/NO_WEBSOCKET/NO_EXAMPLES/NO_TESTS, /MT static CRT, MSVC; link + run a C-API loopback (also validates `rtcDataChannelInit`/reliability struct). Spike lives in the session scratchpad (`net_spike/`), not the repo. **IN PROGRESS**
- [ ] **Phase 1 — seam**: `net_transport.h/.cpp` (interface + factory→nullptr + `net_available()`), `net_transport_rtc.cpp` (all inside `#ifdef NEWTONIA_NET_RTC`), `net_transport_web.cpp` (all inside `#ifdef __EMSCRIPTEN__`), `net_protocol.h`. No behavior change; every build globs them fine.
- [ ] **Phase 2 — native backend**: NEWTONIA_NET option block in xbox/CMakeLists.txt (FetchContent per pins above, link `LibDataChannel::LibDataChannelStatic`); fill rtc backend (2 channels "rel"/"unrel", mutex inbound deque, atomics, non-trickle: expose SDP only at RTC_GATHERING_COMPLETE); `NEWTONIA_NET_SELFTEST=1` env → in-process loopback PASS/FAIL log in xbox_main.cpp.
- [ ] **Phase 3 — web backend**: EM_JS `Module.__nwnet` (pc, rel, unrel, inbox[], state, localDesc), poll-style; `navigator.clipboard` helpers (gesture-gated read).
- [ ] **Phase 4 — savegame streams**: `Save::Stream`/`FileStream`/`MemStream`; `wv/rv/wa/ra` + per-type helpers `FILE*`→`Stream&`; `serialize_game`/`deserialize_game` (no magic/version — save_game/load_game wrap). MUST verify byte-identical save output (fc /b) before landing.
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

- **2026-07-04**: Plan approved. Pins verified via `git ls-remote` (MbedTLS v3.6.6, libdatachannel v0.24.5). Phase 0 spike written (scratch CMake project + C-API loopback `main.c` exercising rtcCreateDataChannelEx with `reliability.unordered/unreliable/maxRetransmits=0` — the exact struct the real backend needs). **Spike not yet run**: `cmake` is not on PATH on this machine; VS 2022 **Build Tools** live at `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools` (found via vswhere) — next step is to run the spike from a `vcvars64.bat` environment (which puts the bundled cmake/ninja on PATH), same as `.github/workflows/xbox.yml` does. Spike files are committed at `netplay_spike/` (CMakeLists.txt + main.c — delete the directory once Phase 2 lands its real CMake block and self-test). To run it: from a vcvars64 shell, `cmake -B netplay_spike/build -S netplay_spike && cmake --build netplay_spike/build --config Release && netplay_spike\build\Release\net_spike.exe` (or the Ninja single-config path). Expected output: `SPIKE PASS`. Work moved to a branch on cradle/newtonia to continue there.
