# Xbox Console Netplay — Milestone Plan

Companion to `xbox/PORT_PLAN.md` (the console port) and `NETPLAY.md` (the
cross-platform online co-op, which lives on the `claude/netplay-md-plan-mjas7b`
branch). This document plans the one multiplayer milestone the netplay effort
explicitly deferred: **online co-op on the actual Xbox Series console.**

> Implementation note: the netplay stack (`net_transport.*`, `net_session.*`,
> `net_lobby.*`, `net_protocol.h`, `net_signal.*`, and the `NEWTONIA_NET`
> block in `xbox/CMakeLists.txt`) exists on `claude/netplay-md-plan-mjas7b`,
> not on `master`. The code work below extends that branch; base the Xbox
> milestone on it.

## 1. Context — why this milestone exists now

Newtonia already ships **cross-platform online co-op** on Steam
(Windows/macOS/Linux), web browsers, iOS, and Android. It uses a title-owned
transport — WebRTC data channels (libdatachannel native / browser
`RTCPeerConnection` web), a Cloudflare Worker for room-code signaling, and a
TURN relay — under a host-authoritative snapshot model. Cross-network play
across those platforms is live and tested (cellular iOS ↔ Wi-Fi Steam Deck).

Two things converged to make Xbox console netplay the next milestone:

1. **The netplay worklog deferred it on purpose** (NETPLAY.md, 2026-07-05):
   *"Xbox netplay is DEFERRED to a future milestone — store-path online
   multiplayer drags in Xbox Live cert requirements (multiplayer privileges,
   parental controls, anonymous-peer policy) that change the architecture…
   dev-mode-only play is the likely first cut."* Accordingly `NEWTONIA_NET`
   is forced **OFF** for `Gaming.Xbox.*` in `xbox/CMakeLists.txt`; the console
   compiles the netplay seam as empty translation units.
2. **Microsoft's ID@Xbox outreach** flagged the cross-network policy surface
   (XR-007), the Partner Center web-service/S2S setup, and PlayFab
   (Foundation Mode / Party) as the recommended cross-network stack, and asked
   which networks our cross-network mode is compatible with.

We answered Microsoft's networks question by email (see §8) and now plan the
engineering.

## 2. Current state recap

| Layer | Today | Runs on console? |
|-------|-------|------------------|
| Sync model (host-authoritative snapshot, `net_session`/`net_protocol`/`GLGame` net code) | Done, platform-neutral | **Yes** — pure game logic, transport-agnostic |
| Transport (`NetTransport` seam) | WebRTC: libdatachannel (native) / `RTCPeerConnection` (web) | **Unknown** — the open question (§4) |
| Signaling + relay | Cloudflare Worker room codes + STUN/TURN | Unknown (same socket question) |
| Identity | Anonymous peers (`is_local_player`; HELLO carries only save-format version) | Gap for XR-007 |
| Invites | Room codes + Steam rich-presence join + OS share sheet | No Xbox-native invite/join yet |

The console also **cannot render yet** — that is a hard prerequisite, not part
of this milestone (see §3).

## 3. Prerequisites (external to this milestone)

1. **Console bring-up** — `xbox/PORT_PLAN.md` Phases 2–3: the GLES2 renderer
   reaching the console GPU (GLon12 spike is the recommended path), SDL2 GDK
   windowing/input/audio, and file I/O on the console. **Netplay cannot be
   validated on a console that cannot run the game.**
2. **GDKX + a dev kit** — NDA console SDK and hardware. Per PORT_PLAN Phase 0
   these were being requested; until they are in hand, this milestone is a
   **design + non-NDA public-repo scaffolding** effort (§6, Phase X2 is the
   only phase buildable today).

## 4. The core decision — transport on the console

**Question:** on the console Game OS, can we keep the WebRTC transport, or must
we adopt PlayFab Party?

- **libdatachannel on console** — it is a normal C library (raw UDP + libjuice
  ICE + MbedTLS DTLS). If it builds for `Gaming.Xbox.Scarlett` under GDKX and
  its sockets are permitted on the Game OS network partition, the console joins
  the **existing unified pool** with zero PlayFab. It gives us **none** of the
  Xbox cert plumbing (privilege checks, safety-setting enforcement) — those we
  build ourselves (§6, Phase X2/X4).
- **PlayFab Party** — Microsoft-blessed cross-network transport, free via
  Foundation Mode, and its Xbox-services plugin wires the XR compliance for
  you. **But Party only interoperates with Party**, so a console-on-Party build
  cannot talk to our WebRTC pool unless *every* platform migrates to Party — the
  largest rewrite on the table.

### What PlayFab would replace (if adopted)

| Our stack | PlayFab equivalent | Replaced? |
|-----------|--------------------|-----------|
| WebRTC `NetTransport` | **Party** (mesh, relay/P2P, encrypted, rel+unrel buffers) | Yes — drop-in behind `NetTransport::create()` |
| STUN + TURN relay | **Party** (own relay) | Yes |
| Cloudflare signaling + room codes | **Lobby** (session dir + cross-platform invites) | Yes |
| Anonymous peers | **Identity** (unified Xbox/Steam/PSN/mobile account) | Fills a gap |
| Snapshot sync / protocol | — | **No** — rides on any transport |
| Matchmaking | Matchmaking | N/A — friend-only co-op |

### Decision (locked)

**Phased; spike libdatachannel-on-console first; PlayFab deferred.**

- **Milestone 1 = dev-mode co-op, no PlayFab.** Build the existing WebRTC
  backend for Scarlett and spike whether it connects on a dev kit. If it works,
  the console is in the existing unified pool for free — the cheapest,
  lowest-risk outcome, and one player pool.
- **PlayFab Party stays a documented store-cert fallback behind the same
  `NetTransport` seam.** The seam means this bet is deferrable and additive, not
  a teardown. Only if the spike fails (or cert requires Party for cross-network)
  do we add a Party backend — and then choose unified-migration vs a separate
  Xbox pool.

## 5. XR compliance surface (what actually applies to us)

Newtonia is **data-only co-op**: no voice, no text chat, no user-generated
content. That collapses most of the communication/UGC policy surface.

| Requirement | Applies? | Work |
|-------------|----------|------|
| **XR-007** cross-network: visually identify Xbox users to non-Xbox players | **Yes** | Platform badge on remote player (Phase X2) |
| **XR-007**: disclose on the store PDP if cross-network is *required* | If required | Store listing copy (Phase X4) |
| **XR-015 / XR-045** communication + privacy safety settings | Minimal | No chat to gate; still honor the **cross-network play** privilege (Phase X4) |
| **XR-018** UGC | No | Newtonia has none |
| Anonymous-peer policy, parental controls | Yes (store) | XUser sign-in + privilege checks (Phase X4) |

## 6. Phases

### Phase X0 — console bring-up (prerequisite, see PORT_PLAN)
Not netplay work; blocks everything console-side. Renderer + SDL GDK + file I/O
on the dev kit.

### Phase X1 — libdatachannel-on-console spike (the fork-decider; needs GDKX + dev kit)
Build MbedTLS + libdatachannel for `Gaming.Xbox.Scarlett` (reuse the proven
FetchContent recipe + the three interop fixes + `mbedtls_user_config.h`), then
run the existing `NEWTONIA_NET_SELFTEST` loopback and a real STUN/TURN connect
on the kit.
- **Pass** → flip `NEWTONIA_NET` ON for Scarlett; console joins the unified
  pool. Proceed to X3.
- **Fail** (sockets restricted / won't certify) → PlayFab Party backend becomes
  required; open the pool decision.

Exit: a definitive yes/no on WebRTC on the console, logged.

### Phase X2 — non-NDA compliance seams (buildable **today**, no hardware)
All platform-neutral, all improve the existing shipping netplay, all give the
private Xbox backend a place to land — mirrors the `Achievements` / `Presence`
seam pattern.
1. **Platform identity on the wire.** Extend `MSG_HELLO`/`MSG_WELCOME`
   (`net_protocol.h`, `net_session.*`) with a `peer_platform` enum
   (Web/Steam/iOS/Android/Xbox/Desktop) + a display-name string; store it on the
   remote `GLShip`/`Ship`.
2. **`net_local_identity()` seam** — platform-neutral local platform id +
   display name (default generic; Steam backend returns the persona; Xbox
   backend in the private mirror returns the gamertag). Same build-flag pattern
   as `steam_presence.cpp`.
3. **Platform badge UX** — render the remote player's platform + name in the
   lobby (`net_lobby.cpp`) and a small tag by the remote ship / HUD
   (`view/overlay.cpp`). Directly satisfies XR-007's visual-identify clause, and
   is a genuine quality-of-life win on every platform now.
4. **`net_policy` seam** — `net_online_play_allowed()` /
   `net_comms_allowed_with(peer)`, default allow-all, wired at the menu ONLINE
   gate and at connect. Xbox backend (private) implements the `XUser` privilege
   checks; default backend keeps every other platform unchanged.

### Phase X3 — dev-mode co-op on the dev kit (needs X0 + X1 pass)
Get two dev kits (or a kit + a PC) into a co-op session through the existing
lobby, Xbox Dev Mode, no store cert. Validate pad hot-plug, disconnect/rejoin,
snapshot bandwidth at late generations on console hardware.

Exit: full online co-op loop stable on a dev kit.

### Phase X4 — store cert (deferred; own sub-milestone)
XUser silent sign-in at boot; cross-network-play privilege check gating the
ONLINE row; sign-out mid-game → pause + menu; **Xbox-native invite/join** (MPA
against our session service, or PlayFab Lobby); XR-007 PDP disclosure; the
PlayFab-Party-vs-WebRTC cert conversation (§4); packaging + submission. If Party
is required, decide unified-migration vs separate Xbox pool here.

## 7. Code work-item list

| # | Item | Files | Phase | Where |
|---|------|-------|-------|-------|
| 1 | Scarlett netplay build block (ready-to-enable, still OFF so console-smoke stays green) | `xbox/CMakeLists.txt` | X1 | Public |
| 2 | libdatachannel-on-console socket spike | build + dev kit | X1 | Private/hardware |
| 3 | Platform id on the wire (HELLO/WELCOME) | `net_protocol.h`, `net_session.*` | X2 | Public |
| 4 | `net_local_identity()` seam + default/Steam backends | new `net_identity.*`, `steam_*` | X2 | Public |
| 5 | Platform badge UX (lobby + HUD) | `net_lobby.cpp`, `view/overlay.*` | X2 | Public |
| 6 | `net_policy` seam (default allow-all) | new `net_policy.*` | X2 | Public |
| 7 | XUser sign-in + privilege checks | `xbox_main.cpp`, private `net_policy` backend | X4 | Private |
| 8 | Xbox-native invite/join (MPA or PlayFab Lobby) | private | X4 | Private |
| 9 | PlayFab Party `NetTransport` backend (only if X1 fails / cert requires) | new `net_transport_party.cpp` | X4 | Private |
| 10 | Store PDP cross-network disclosure | store listing | X4 | Partner Center |

## 8. What Microsoft was told (cross-network declaration)

Reply sent in response to the ID@Xbox outreach:
- **Cross-network partners today:** Steam, web/browser, iOS, Android. **Not** on
  PSN or Nintendo.
- Multiplayer is co-op only; **no voice/text chat, no UGC** (shrinks XR-015/045
  and XR-018).
- Xbox online co-op is the next milestone: dev-mode validation first, then the
  store/cert path, evaluating PlayFab Foundation Mode / Party.
- Open questions posed to Microsoft: (1) Foundation Mode eligibility for our
  title; (2) whether a title-owned cross-network transport (our WebRTC stack) is
  acceptable for cert or Party is effectively required; (3) whether we still need
  a Relying Party / Business Partner Certificate given a PlayFab-based path.

Their answer to (2) directly decides §4 for the store build.

## 9. Risk register

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| libdatachannel sockets restricted on Game OS | Medium–High | Phase X1 spike decides early; PlayFab Party is the fallback behind the same seam |
| Cert rejects a non-Party cross-network transport | Medium | Question (2) to Microsoft (§8) settles it before we build; Party backend is scoped as fallback |
| Party required → forces pool decision | Medium | Seam makes a Party backend additive; unified-migration vs separate Xbox pool documented, not blocking |
| Console bring-up (X0) slips | High | Out of scope here; Phase X2 seams proceed on all existing platforms regardless |
| Snapshot bandwidth on console late-game | Low–Medium | Already have 5 Hz fallback + delta snapshots (NETPLAY.md M2) |
| No dev kit yet | High (today) | Phase X2 is fully buildable/testable without hardware; X1/X3/X4 staged for the kit |
