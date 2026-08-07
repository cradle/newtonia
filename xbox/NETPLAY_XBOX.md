# Xbox Console Netplay — Milestone Plan

> **DEFERRED (2026-07-30): this whole milestone is owned by the private repo
> `cradle/newtonia-xbox`** — see `xbox/PRIVATE_REPO.md`. That includes the two
> §7 items that carry no NDA content (8c host-side pose/fire sanity bounds,
> 8d malformed/invalid-auth fuzzing of `Net::Reader`): the private repo drives
> them because they are Xbox-motivated, and either one comes back as an
> ordinary PR against `cradle/newtonia` when it's done. Phases X0/X1/X3/X4 all
> need GDKX and a dev kit. Nothing on this page is scheduled work for the
> public repo; it stays as the reference and handoff material.

Companion to `xbox/PORT_PLAN.md` (the console port) and `NETPLAY.md` (the
cross-platform online co-op). This document plans the one multiplayer milestone
the netplay effort explicitly deferred: **online co-op on the actual Xbox
Series console.**

> Implementation note: the netplay stack (`net_transport.*`, `net_session.*`,
> `net_lobby.*`, `net_protocol.h`, `net_signal.*`, and the `NEWTONIA_NET`
> block in `xbox/CMakeLists.txt`) shipped to `master` in PR #323 (2026-07-18)
> — the earlier `claude/netplay-md-plan-mjas7b` note is obsolete. The code
> work below extends what is on `master`; base the Xbox milestone on the
> private repo's mirror of it.

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

> **Primer — PlayFab Lobby vs PlayFab Party (they are two different products).**
> Both carry the "PlayFab" name but do completely different jobs, and you can use
> either without the other:
> - **PlayFab Lobby = the meeting room.** It helps two players *find each other*
>   and swap a little connection info ("who's here, how to reach me"). It carries
>   **no gameplay** — it's just the introduction. *Our* equivalent is the
>   **Cloudflare Worker + 5-char room code**. (In this doc, "signaling" =
>   this meeting-room job.)
> - **PlayFab Party = the phone line.** Once players are introduced, Party is the
>   *network pipe* that carries the gameplay (and voice). *Our* equivalent is our
>   **WebRTC/DTLS** connection. (In this doc, "transport" = this phone-line job.)
>
> | The job | PlayFab's product | Our version (today) |
> |---------|-------------------|---------------------|
> | Find each other + swap connection info | **Lobby** | Cloudflare Worker + room code |
> | Carry the actual gameplay | **Party** | WebRTC/DTLS |
>
> The whole plan in one line: **keep our phone line (WebRTC) always; maybe borrow
> PlayFab's meeting room (Lobby) on Xbox if the console can't reach our own; never
> adopt PlayFab's phone line (Party)** — Party can't talk to WebRTC and has no web
> SDK (§4a), so adopting it would strand web players. Everything else (bridges,
> adapters, legs, rungs) is detail about keeping the meeting-room job working on
> Xbox without giving up our phone line.

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
  largest rewrite on the table — and Party has **no browser/web target** (§4a),
  so a full migration strands our web players entirely.

### What PlayFab would replace (if adopted)

| Our stack | PlayFab equivalent | Replaced? |
|-----------|--------------------|-----------|
| WebRTC `NetTransport` | **Party** (mesh, relay/P2P, encrypted, rel+unrel buffers) | Yes — drop-in behind `NetTransport::create()` |
| STUN + TURN relay | **Party** (own relay) | Yes |
| Cloudflare signaling + room codes | **Lobby** (session dir + cross-platform invites) | Yes |
| Anonymous peers | **Identity** (unified Xbox/Steam/PSN/mobile account) | Fills a gap |
| Snapshot sync / protocol | — | **No** — rides on any transport |
| Matchmaking | Matchmaking | N/A — friend-only co-op |

### 4a. Platform coverage — Party's web gap

PlayFab has two layers with different reach:

- **PlayFab Party** (the networking/voice SDK that would replace our transport)
  ships **native** SDKs for **Xbox, PlayStation 4/5, Nintendo Switch (+ Switch 2),
  Windows/PC, Linux, macOS, Android, iOS** (plus C/C++, Unity, Unreal). macOS is
  a real C/C++ target — universal binary incl. Apple Silicon, minimum macOS
  12.3. **The one platform with no Party SDK is the browser/WebAssembly build.**
- **PlayFab backend services** (identity, lobby, matchmaking, economy) are
  REST/service-based and platform-agnostic; identity federates **Xbox,
  PlayStation, Nintendo Switch, Steam, Epic, and mobile** accounts.

Two consequences for us:

1. **Party covers every platform Newtonia ships on *except the browser build*.**
   Our web (`RTCPeerConnection`) players cannot run Party at all. A "migrate
   everyone to Party" pool would drop web entirely (or keep it on a separate
   WebRTC pool — defeating the point of unifying).
2. **Steam is an identity provider to Party, not a Party runtime target.**
   Foundation Mode can federate a Steam account, but Party runs on the *Windows*
   build — which is fine for us, since our Steam build *is* the desktop build.

This is another mark against the full-migration option and a reason the plan
keeps Party as a seam-level, console-scoped fallback rather than a wholesale
transport swap.

| Platform (Newtonia ships) | WebRTC (today) | PlayFab Party |
|---------------------------|:--------------:|:-------------:|
| Steam desktop — Windows | ✅ | ✅ |
| Steam desktop — Linux | ✅ | ✅ |
| Steam desktop — macOS | ✅ | ✅ (C/C++ SDK; universal incl. Apple Silicon; min macOS 12.3) |
| Web / browser | ✅ | ❌ **no web SDK** |
| iOS | ✅ | ✅ |
| Android | ✅ | ✅ |
| Xbox console | ❓ (X1 spike) | ✅ |

### 4b. Relay / TURN — bundled-only, not a standalone service

Party includes its own **Azure-hosted cloud relay**: it attempts NAT
punch-through (direct P2P) first and **automatically falls back to the relay**
when that fails — functionally the job our TURN does — with no relay servers to
provision and no credentials to mint (today the signaling worker mints
Cloudflare TURN creds per session).

The catch: Party is a **proprietary UDP stack, not standard WebRTC**, and its
relay is **internal to Party** — it is *not* exposed as a generic STUN/TURN ICE
endpoint you can point libdatachannel at. There is no "bring your own WebRTC,
borrow PlayFab's relay" option.

Consequences:

1. **You can't cherry-pick Party's relay.** Replacing our Cloudflare TURN with
   Party's relay means adopting Party wholesale (transport included) — same
   all-or-nothing property as the web gap (§4a).
2. **WebRTC-on-console still needs its own TURN.** If the X1 spike passes and we
   keep WebRTC on the console, we must confirm a TURN provider reachable from the
   Game OS network partition (our Cloudflare TURN, or a self-hosted coturn) — the
   spike must exercise a *relayed* connect, not just direct/LAN.

### 4c. Cost — PlayFab Foundation Mode (the "free PlayFab")

If we adopt PlayFab, the relevant tier is **Foundation Mode** — no-cost PlayFab
for Xbox-ecosystem developers under the Xbox publishing framework (no payment
instrument, no Azure subscription).

- **Eligibility:** register a studio in PlayFab Game Manager; ship (or plan to
  ship) on Xbox; link PlayFab to the Partner Center studio + product.
  **Preview caveat: new PlayFab titles only** — existing titles can't migrate
  until a path lands ~mid-2026 (fine for us; our PlayFab title wouldn't exist
  yet).
- **Included, free:** Identity (cross-play accounts), **Lobby**,
  **Matchmaking**, **Party Networking**, real-time messages, cloud game saves,
  statistics, friends/groups, leaderboards, catalog/inventory, title
  data/comms, service telemetry.
- **Not included (returns a permissions error):** **Multiplayer Servers**
  (dedicated hosting), UGC economy, CloudScript classic, Segmentation/
  Experimentation/Churn/CDN, custom telemetry events, legacy v1 APIs. Azure
  Functions *invocations* are free but the underlying compute (GB-s) is billed
  via Azure.
- **Limits:** no monthly usage caps and no per-op charges within the included
  set — only per-API, per-player rate limits (~30 calls / 2 min).

**What this means for us:** everything the store-cert path would touch —
**Party (transport), Lobby (invites), Identity (cross-play)** — is in the free
set. The one big exclusion, **Multiplayer Servers**, doesn't apply: Newtonia is
P2P host-authoritative, not dedicated-server. We also have no UGC. So Foundation
Mode covers 100% of the plan's PlayFab needs at no cost. (Context: PlayFab cut
its general free tier from ~100K to ~1K MAU in March 2026; Foundation Mode is
the Xbox-dev replacement.)

### 4d. Could we run dual backends?

Yes — usefully — because the sync layer (`net_session`, `net_protocol`,
snapshots) rides *on top of* the `NetTransport` seam and is completely
transport-agnostic. "Which backend" is a transport + lobby concern, never a
simulation one. Two distinct meanings, only one of them cheap:

**Meaning 1 — one build, two `NetTransport` impls, chosen per session ✅**
The build links **both** a WebRTC backend and a Party backend and picks per
connection: a web/Steam/mobile friend → WebRTC (existing pool); an Xbox friend
or an Xbox-invite arrival → Party (certified path). Generalize
`NetTransport::create()` to `create(backend)` and let the lobby choose. **Every
individual 2-player session stays homogeneous** (both ends same backend) — what
you gain is a *build* that can reach **both pools** depending on the peer, so an
Xbox player isn't forced to choose between the existing cross-platform community
and certified Xbox invites. This is the version worth having.

**Meaning 2 — a web player and an Xbox player in the *same* session, across
different transports ❌** Not possible by running both backends: WebRTC
(DTLS/SCTP) and Party (proprietary UDP) are different wire protocols; bridging
them needs a **translating relay server** running both stacks — a dedicated
server component, which Foundation Mode excludes (§4c) and which turns the clean
P2P design into client-server. Not worth it for 2P. (Our star topology *could*
let a host bridge for 3+ players, but Newtonia is 2P — only ever one remote peer,
so the host just speaks that peer's backend.)

**Scope note:** this ❌ is about bridging two different *transports* (WebRTC ↔
Party UDP), which forces a gameplay-relaying server. It does **not** apply to
bridging two *signaling* backends while both peers keep the **same** WebRTC
transport (PlayFab doing Lobby-only, not Party) — that is a lightweight
*signaling* bridge that never touches gameplay and is **not** the excluded
Multiplayer-Server case. See the signaling fallback ladder in §5a.

Gates on Meaning 1:
- **Console viability** — carrying WebRTC on console needs the **X1 spike** to
  pass. If it fails, console is Party-only and dual-backend isn't available
  *there* (desktop/mobile could still be dual).
- **Cert** — a retail-Xbox WebRTC session only adds value if Microsoft blesses a
  non-Party cross-network transport (the §8 question). If they require Party for
  cross-network on Xbox, dual-backend collapses to Party-only on console.
- **Cost** — two transport stacks to build/link/test on one target
  (libdatachannel+MbedTLS *and* the Party SDK), bigger binary, doubled failure
  modes, and lobby routing to the right backend.

If both gates go our way, dual-backend is the option that avoids ever stranding
web players *and* gives Xbox a certified path — worth revisiting once the X1
spike and the cert answer land.

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
| Secure game communication (**XR-134 retired** — now best-practice guidance) | Guidance | Encrypt all traffic; **already met** — libdatachannel DTLS/MbedTLS. Verify vs the (NDA) best-practices doc in Phase X4 (§8a) |
| **XR-014** protect PII incl. the **XUID** | Yes (light) | Auth (Path A/B) surfaces XUID/gamertag. Mitigation: badge uses the **display name, not XUID**; PII only over encrypted DTLS (already so); **don't persist** peer PII; no PII at rest (we have no DB). Cert-tested |

### 5a. Secure-communication table — how our stack maps

The GDK *Best Practices for Secure Game Communication* doc summarises its
guidance in a table it explicitly labels **"best practice *recommendations*"** —
not mandates. With XR-134 retired and Microsoft's written confirmation that our
own stack is acceptable (§8a), this is a recommended shape to follow, not a cert
gate. We have **three** communication legs, landing on different rows:

| Our leg | Table row (recommended API) | Encryption | Authentication |
|---------|-----------------------------|:---:|:---:|
| Signaling — WebSocket to our room-code service | WebSocket/TCP → TLS + **XSTS** (libHttpClient) | ✅ WSS/TLS | ❌ anonymous room codes (no XSTS) |
| Gameplay — WebRTC data channels | Peer-to-Peer/UDP → secure platform API (**PlayFab Party**) | ✅ **DTLS** | ⚠️ transitive (whoever has the code) |
| — (we have no dedicated server) | Client/Server/UDP → QUIC (MsQuic) | n/a | n/a |

- **Encryption is already met on both legs we have** (DTLS + WSS/TLS) — that half
  of every recommendation is satisfied.
- **The gap is authentication**, and it's a *recommendation*: XSTS on the
  signaling/web leg, and authenticated peers on the P2P leg. Today peers are
  anonymous (room codes) — fine on Steam/web, the thing the Xbox guidance
  suggests tightening.

**XSTS** = Xbox Secure Token Service: after XUser sign-in, request an XSTS token
scoped to your service (relying party), send it with the call, service validates
it → proves a real signed-in Xbox user. It applies to our **signaling** leg, not
the gameplay UDP (the P2P row's auth answer is "use a secure platform API").

**Roll-our-own is explicitly sanctioned** (NDA `gc-secure-game-mesh-impl`, read
2026-07). The Client/Server section: *"If you would like to build your own
protocol… use **DTLS**… with **server TLS certificate authentication and client
XSTS token authentication**,"* and it **recommends the third-party OpenSSL
library** for DTLS (keep it current at release + patch CVEs). So third-party
crypto is fine — even recommended — for the *transport* layer (the "avoid
third-party libs" rule is HTTP/WebSocket-specific, §5b-console). We use MbedTLS;
they name OpenSSL — libdatachannel supports **both** backends (a decision point,
not a blocker). **Key insight:** "client XSTS authentication" needs whoever
validates it to hold the relying-party **private key** — which **cannot live on
a peer console** (no-hard-coded-secrets). So in P2P, auth **must be brokered by a
service**, never peer-to-peer. That is *why* Party is "strongly recommended" for
P2P, and it makes A and B clean siblings — both keep our DTLS transport, differing
only in *who validates identity*:

Three ways to close the auth-recommendation gap, increasing PlayFab reliance:

- **A — own XSTS-validating web service.** Our signaling validates an XSTS token
  before issuing/relaying a room. The heavyweight Relying Party + Business
  Partner Certificate + NSAL setup (`live-web-services`). All in-house; most work.
  **Confirmed mechanics** (NDA `live-security-tokens`, read 2026-07): a title
  XSTS token is minted for a **relying party we must register in Partner Center**;
  the token is a **JWE** — our server holds the relying-party **private cert
  key**, decrypts the content-encryption-key section (matched by the header
  `x5t`), AES-decrypts the payload to the inner JWT claims (xuid/uhs), and
  enforces the signature policy. Feasible (JOSE / Worker WebCrypto) but it's a
  high-value private key to custody, needs the Managed-Partner "Relying Parties"
  permission, and covers only the Xbox identity (cross-network still needs
  per-platform validation). **This is the maximally web-friendly path** — the
  rendezvous never leaves our Worker, so web peers ride along unchanged — so it's
  worth spelling out how cross-play actually works under it.

  **How cross-play works with rolled-own identity + broker.** Identity sits
  *beside* the existing rendezvous; it changes neither the transport nor how peers
  pair. Three layers, only the middle one is new:
  - **Transport (unchanged):** WebRTC/DTLS, already cross-platform. Nothing here
    cares about identity.
  - **Broker/rendezvous (extended):** the Worker still keys a room by its 5-char
    code and relays SDPs. New step: on connect a peer presents a platform token;
    the broker **validates it and attaches a normalized identity**
    `{platform, platform_id, display_name}` to that peer's room presence. The
    offer/answer exchange is otherwise identical for every platform.
  - **Identity (new, per-platform):** each client gets a token from its own
    OS/store before connecting; the `net_local_identity()` seam (Phase X2, item
    #4) is the carrier.

  Concrete flow — **Xbox host ↔ web joiner**:
  1. Xbox signs in via `XUser`, requests an XSTS token scoped to our relying
     party, opens a room, sends the token with the host connect. Broker decrypts
     the JWE with the private key → resolves XUID/gamertag → mints the room code.
  2. Web player has no platform token → joins with the room code **anonymously**;
     broker relays the host's stored offer as usual.
  3. Xbox client sees the peer flagged "web / unauthenticated"; its
     **CrossNetworkPlay privilege (185)** governs whether that's allowed — exactly
     the intended cross-network semantics (the other network's users are not
     Xbox-authenticated by definition).
  4. WebRTC/DTLS connects them (relay-only if forcing TURN, §5b). Gameplay flows.

  So cross-play doesn't "break" under rolled-own identity — the room code stays
  the pairing mechanism; auth just decorates peers and lets each side apply policy.

  **What it buys:** authenticated peers (satisfies the XSTS recommendation without
  PlayFab); trusted display names/badges for XR-007 (from validated identity, not
  self-reported HELLO strings); policy enforced at the broker before SDPs relay;
  and **web stays first-class** because the rendezvous never moves off our Worker.

  **The real cost — identity is N separate implementations.** XSTS validates
  **Xbox identity only**. Every *other* authenticated platform needs its own
  server-side validation against that platform's web API — Steam
  `AuthenticateUserTicket` (publisher key), Game Center, Play Games — each a
  distinct broker code path. **Web has no platform identity at all:** either accept
  anonymous web peers (fine for cross-network, per step 3) or build our own
  account/OAuth for web (a whole product). And there is **no unified account** —
  unlike PlayFab Identity (which federates Xbox/Steam/mobile into one linked
  account), a rolled-own broker validates each native token separately; friend-only
  room-code co-op doesn't need federation (only "this peer is a real signed-in user
  named X"), but any "same account across platforms" wish is ours to build. This
  N-validators burden — on top of the private-key custody above — is precisely the
  work Path B's preconfigured relying party removes.
- **B — PlayFab for identity. ⭐ recommended.** PlayFab **is a preconfigured
  relying party** — its "Login with Xbox" performs exactly the XSTS/JWE validation
  above internally and returns a PlayFab session ticket, giving us authenticated
  peers **without** the relying-party/BPC setup, the private-key custody, or the
  crypto. **Path B is two separable sub-shapes — Identity and Lobby are different
  products (§4 primer), and using PlayFab for *tokens* does not require it for
  *rendezvous*:**
  - **B1 — Identity only, keep our Worker rendezvous. ⭐ the actual default.** The
    Xbox peer authenticates via PlayFab (Login with Xbox → ticket) and presents the
    ticket to **our own Worker**, which validates it server-side (PlayFab
    `AuthenticateSessionTicket`, title secret) and then runs the normal room-code
    rendezvous. **One pool for everyone — Xbox, PC, web, mobile — web fully
    preserved, no PlayFab Lobby, no bridge/hub.** PlayFab does the hard key custody;
    we keep single-pool rendezvous. This is ladder rung 1/2 (§5a-i) with
    PlayFab-authenticated peers layered on.
  - **B2 — Identity *and* Lobby (PlayFab does rendezvous too).** Only when the
    console **can't reach our Worker at all** for rendezvous (X1 leg 2 fails *and*
    a libHttpClient client to our Worker isn't viable). Then the Xbox peer is forced
    onto PlayFab Lobby, the pool fragments, and cross-network with web needs the
    signaling **bridge / hybrid hub** (§5a-i rung 4, `xbox/NETPLAY_BRIDGE.md`). Note
    this is driven by the console **network sandbox**, not by the token choice.
  Either way the DTLS **transport** seam is untouched.
- **C — full PlayFab Party on Xbox.** The table's literal P2P recommendation
  (auth+encryption+relay handled) = the dual-backend option (§4d), with its
  pool-split / web-gap / two-stack cost.

**Plan:** the threat surface is tiny (2-player friend co-op, host-authoritative,
no chat/UGC/PII on the wire), so don't over-build. Hold for the forum answer
(§8a) on how strictly the auth recommendation applies, then **lean Path B1**
(PlayFab identity, our own Worker rendezvous) — it follows the recommendation,
keeps our transport *and* one unified pool (web safe), and rides free Foundation
Mode. Fall to **B2** (PlayFab Lobby too, → the bridge/hub) only if the console
can't reach our Worker. All Phase X4; dev-mode co-op (X3) needs none of it.

**XSTS forces a *service broker*, not PlayFab — and auth is a separate axis from
rendezvous.** It's easy to read "we need XSTS" as "we must adopt PlayFab." Two
corrections that keep the door open on our own stack (and on web):

1. **XSTS mandates a validating service, not PlayFab specifically.** Validating a
   client XSTS token needs the relying-party **private key**, which can't live on
   a peer console — so the validation can't be peer-to-peer; it must be brokered
   by *a* service. That service can be **our own web service (Path A, no PlayFab
   at all)** or **PlayFab (Path B)**. PlayFab is *recommended* only because it is
   a preconfigured relying party — it spares us the private-key custody, the
   Partner Center "Relying Parties" permission, and the JWE crypto. That is
   convenience, not a mandate. And XSTS auth is itself only a **best-practice
   *recommendation*** (XR-134 retired, §8a) that first applies at store cert (X4);
   the hard obligation — encryption — is already met by DTLS. Dev-mode co-op (X3)
   owes none of it.

2. **PlayFab-for-identity ≠ PlayFab-for-rendezvous — they are separable
   decisions.** PlayFab **Identity** ("Login with Xbox" → validated token /
   session ticket) and PlayFab **Lobby** (the signaling/rendezvous that pairs two
   peers) are *different* PlayFab features. Path B above bundles them for
   convenience, but adopting PlayFab Identity to satisfy the XSTS recommendation
   does **not** by itself require replacing our rendezvous. You can authenticate a
   peer through PlayFab Identity (or Path A) while the *rendezvous* still rides our
   own room-code signaling.

**Why the rendezvous axis is the one that decides web's fate.** Our signaling is
a single shared rendezvous: a room is keyed by its 5-char code, and a host and a
joiner **must reach the same signaling service** to exchange SDPs — two peers on
different signaling backends never meet (no bridge exists). Consequences:

- **Cross-network play — including web — stays alive exactly as long as the Xbox
  build keeps speaking our room-code rendezvous.** That leg is gated on the X1
  spike **leg 2** (does WSS-on-console survive the cert-store-locked sandbox, §6).
  If it passes, Xbox joins the existing unified pool with **no PlayFab** and web
  is untouched.
- **Moving Xbox *rendezvous* to PlayFab Lobby is what would strand web.** A
  PlayFab-Lobby Xbox peer can't rendezvous with a Worker-signaled PC/web peer
  without a **bridge service** the codebase doesn't have. So a PlayFab-Lobby
  fallback realistically means "Xbox↔Xbox via PlayFab, cross-network via the
  Worker" (genuinely dual-signaling), *not* a wholesale rendezvous swap. Full
  migration to **PlayFab Party** is the only path that truly "leaves web for
  dead" (Party has no web SDK, §4a) — and it is the option this plan explicitly
  rejects.

**Web-preserving default (recommended):** keep the room-code rendezvous on Xbox
(pending X1 leg 2). If the XSTS recommendation is taken up, layer PlayFab Identity
(or Path A) **on top of** that rendezvous rather than swapping the rendezvous out.
Only fall to PlayFab Lobby rendezvous if leg 2 fails — and even then keep the
Worker path for cross-network (or accept a bridge), so web is never dropped.

### 5a-i. Signaling fallback ladder (if the Worker WS won't run on console)

The X1-leg-2 fallback ("move signaling to libHttpClient **or** PlayFab Lobby")
is really an **ordered ladder** — take the earliest rung that works, because each
later rung costs more and risks web. First disambiguate what "run both signaling
backends" means, because the two readings have very different cost:

- **Two *client transports* for our own Worker (cheap, one pool).** The console
  problem in leg 2 is the *client library*, not our Worker *protocol* —
  libdatachannel's bundled WebSocket may be blocked and the cert store is locked.
  The `NetSignal` seam already abstracts "how you open a WebSocket" (native vs
  browser); a **libHttpClient/XCurl WebSocket client to the same Worker** is a
  third sibling. Same broker, same unified pool, **no fragmentation, web fully
  preserved, zero PlayFab.** This is Option 2 and the first rung to try.
- **Two *brokers* (PlayFab Lobby *and* our Worker).** Different rendezvous
  services → **fragmented pools** (both peers must share a backend). Worth it only
  for **certified native Xbox invites** (XR-124), not merely to get signaling
  working. This is Option 1; keep sessions homogeneous and let the **join method
  pick the backend** — arrived via Xbox native invite → PlayFab Lobby; typed a
  room code → Worker. The player never picks a "mode"; how they connected decides.

**Can the two brokers communicate? Yes — via a *signaling* bridge.** If Xbox is
stuck on PlayFab Lobby yet we still want cross-network with web, a bridge can pair
a PlayFab-Lobby peer with a Worker peer. Full strategy — topology, code↔lobby
mapping, message flow, TURN normalization, auth, and open items — is in
**`xbox/NETPLAY_BRIDGE.md`**, which also generalizes the bridge into a
protocol-agnostic **hybrid signaling hub** (PlayFab + Worker as the first two
*adapters*, extensible to PSN/Switch/etc.; §12 there). In brief, it works because:

- Signaling is just **opaque SDP/ICE blobs** — transport-neutral; the bridge
  forwards strings and needn't understand either session model.
- **Both peers stay WebRTC/DTLS** (PlayFab = Lobby-only, not Party), so there is
  **no transport translation**. Once the handshake is mirrored, the peers connect
  **P2P (or via a shared TURN)** and the **bridge leaves the data path** — it lives
  only for the setup handshake. So it is **not** the gameplay-relaying
  Multiplayer-Server case Foundation Mode excludes (contrast §4d Meaning 2).

Shape: extend the Worker (or a sidecar) with a **PlayFab server-API client** that
mirrors `offer`/`answer`/`cand` between a Worker room and a PlayFab Lobby keyed by
a shared code, and **normalizes TURN** (hand our Cloudflare creds to both sides —
dovetails with relay-only, §5b). Costs/opens: a new always-on service component;
PlayFab server-side lobby read latency (PubSub vs polling — setup-only, not
in-session); room-code↔lobby mapping; the PlayFab secret lives server-side on the
bridge (fine — never shipped). Auth asymmetry is **correct, not a bug**: a bridged
web/PC peer is anonymous = a cross-network (non-Xbox) participant, exactly what the
CrossNetworkPlay privilege governs; the Xbox↔Xbox auth guarantee is untouched.

**The ladder, earliest rung first:**
1. **Worker-native** (libdatachannel WS) — leg 2 passes → one pool, no change.
2. **libHttpClient/XCurl → our Worker** — leg-2 client fix, still one pool, no
   PlayFab, web safe. *Prefer this.*
3. **Dual broker + join-method switch** — add PlayFab Lobby for certified native
   invites; accepts pool fragmentation (Xbox↔Xbox on PlayFab, cross-network on the
   Worker).
4. **Signaling bridge** — pairs the two brokers so cross-network survives even when
   Xbox is Lobby-only; the "have both" option, at the cost of running the bridge.
5. *(last resort)* accept fragmentation permanently, or full PlayFab Party (drops
   web, §4a) — the explicitly-rejected end of the ladder.

Specifics from the Authentication guidance that sharpen the above:

- **Service auth is already met.** "Publicly verifiable certificates for services"
  = our signaling is **WSS to a real domain**; the service authenticates itself
  to the client via standard TLS server-cert validation. Only the *client →
  service* direction (a platform token) is open.
- **No hard-coded secrets — we already comply.** TURN credentials are minted
  **server-side per session with a TTL**, not baked into the client; only public
  values ship in the binary (signal URL; later a PlayFab title ID). Keep this a
  design invariant — never embed a static TURN credential or service secret in
  the shipped client.
- **Path B is the *sanctioned* pattern, not a workaround.** The guidance:
  game-service/account tokens are valid "if granted after the platform token has
  been validated." That is exactly PlayFab "Login with Xbox" — it validates the
  Xbox **XSTS** (platform) token, then issues a PlayFab **session ticket**
  (account token). So Path B is explicitly endorsed.
- **"Authenticate all partners"** — the read-only carve-out (MOTD/config) doesn't
  apply; we expose no public read-only service surface, so no exception to
  special-case.

### 5b. Confidentiality — player IP exposure (ICE/STUN)

The Confidentiality guidance calls IP-address hiding **"essential"** (its
strongest wording): exposing a player's IP enables direct attacks (DDoS), so all
communication should route through a relay. This is the highest-priority
secure-comms item — treat it as a real obligation for the Xbox path, even though
XR-134's retirement means it isn't a formal cert gate.

**Our default WebRTC path exposes IPs.** ICE gathers **host** candidates (LAN
IPs) and **server-reflexive** candidates (the public IP, discovered via STUN),
exchanged in the SDP; a direct-connected session (host/srflx path) reveals each
peer's public IP to the other.

**Fix — we already ship the mechanism.** Force **relay-only** (TURN): only relay
candidates are offered, no direct path forms, and each peer sees only the TURN
server's IP. `NetTransport::set_force_relay` and the lobby's relay-only join
exist today (built for TURN testing; they double as IP hiding). **Default it ON
for the Xbox build.**

- **One-sided forcing suffices.** A relay-forced side offers no direct candidates
  *and* drops the peer's direct candidates, so a single Xbox player forcing relay
  hides **both** peers' IPs — protecting the cross-network partner (Steam/web/
  mobile) too.
- **Costs:** added latency (hairpin through TURN) and TURN **egress bandwidth for
  every session** (not just as a fallback) — modest at 2P co-op traffic (small
  input + 10 Hz snapshots), but a real cost line. The relay must be reachable
  from the console Game OS — already in the X1 spike's scope (§4b requires a
  relayed connect).
- **Party alternative:** Party with direct peer connectivity disabled relays by
  default (managed relay, IP hidden) — another store-build point for Party
  (§4d), but our own relay-only already meets the requirement.
- **Scope:** applied to the Xbox build here. Forcing relay for the *existing*
  Steam/web/mobile pool would hide IPs there too but adds TURN cost across all
  sessions — a separate product decision, noted not taken.

### 5c. Integrity — message and logic

Two halves, and we land differently on each.

**Message integrity (transport) — met by construction ✅.** The guidance's caveat
is that *encryption ≠ integrity* (unauthenticated ciphers are malleable). That's
about **un**authenticated encryption. Our transport is **DTLS**, which is
*authenticated* encryption: every record carries a MAC (CBC-HMAC) or AEAD tag
(AES-GCM), so tampering is detected and dropped. Replay/duplication is covered
too — DTLS has a sliding-window anti-replay check (per-record sequence numbers),
and WebRTC data channels run over **SCTP** (TSN sequencing; reliable channel
ordered, unreliable input still DTLS-authenticated). So "secure hashing +
sequence numbers" is provided at the transport; nothing to build. The game-mesh
guidance adds that sequence numbers should live **inside the encrypted payload**
(ours do — DTLS/SCTP counters are within the encrypted record) and, if used as a
security boundary, be seeded from a **CSPRNG** (`BCryptGenRandom`), not `rand()`.
Our anti-replay boundary is DTLS's, not our app-level snapshot ids, so this is
already satisfied.

**Logic integrity (game-state validation) — strong by design, one soft spot.**
Our host-authoritative model already does most of the doc's "reject player speed
over the limit" job: the client sends only **INPUT** (bitmask + analog + one-shot
counters) and the **host runs the physics**, bounding speed/position/fire-rate by
construction — a client can't assert authoritative state, only held inputs.

- **The soft spot:** the client-authoritative latency-hiding paths (client ship
  **pose** PROTO 12, **kill claims** PROTO 13, **shot spawns** PROTO 14+) are
  values the client *asserts*. Kills/shots already have structural integrity — a
  claim references a **host-minted** id (exactly-once by construction, can't
  claim a nonexistent bullet). The gap is mainly **pose/speed**: the host should
  sanity-bound a client-asserted pose (position delta ≤ max-speed·dt + margin,
  fire cadence within cooldown) and **silently discard** outliers — literally the
  doc's example.
- **Stakes are low** (friendly 2P co-op, no in-session ranking; achievements only
  credit local-player ships), so this is modest hardening, not a gate. It's
  **platform-neutral** — helps every platform and hardens against buggy/desynced
  clients, not just malicious ones — so treat it as general netcode hardening
  rather than an Xbox-only item.

### 5d. Implementation references (GDK samples) & the self-signed caveat

The NDA `secure-communication-impl` page lists GDK samples that map to our
architecture — implementation references for the dev-kit engineer (NDA, in the
GDK sample set, not readable here):
- **`NetworkingSecurityOpenSSL`** — self-signed certs, **client-to-client**
  (peer) comms over **OpenSSL + DTLS**. Our P2P pattern almost exactly.
- **`GameService`** — client ↔ web-service comms + **auth checks** per best
  practices → the reference for **Path A** (our signaling Worker validating XSTS).
- **`SimpleWinHttp`** — HTTPS + XSTS via xCurl.

Two guidance points that sharpen our design:
- **Use DTLS, not a bespoke protocol.** The Game Mesh section: *"games are
  strongly encouraged to use DTLS… with client/server authentication through
  platform authentication tokens"* and *"should avoid implementing a custom
  protocol for secure UDP."* We already use **DTLS** (not custom crypto) → aligned.
- **Self-signed certs are not production-ready.** The samples' self-signed certs
  carry: *"For production ready code… encrypt using **key material provided by a
  key service**."* WebRTC uses self-signed DTLS certs by default, so the concrete
  requirement is that the peer's DTLS **fingerprint be vouched by an authenticated
  service** — not trusted blind. That is the same service-brokered-auth conclusion
  (Path A/B, §5a), now pinned at the DTLS-cert level; the guidance calls
  authenticating clients/servers *"essential."*

## 6. Phases

All phases run in the private repo (2026-07-30 deferral); X0 is itself the
deferred console bring-up.

### Phase X0 — console bring-up (prerequisite, see PORT_PLAN)
Not netplay work; blocks everything console-side. Renderer + SDL GDK + file I/O
on the dev kit.

### Phase X1 — libdatachannel-on-console spike (the fork-decider; needs GDKX + dev kit)
Build MbedTLS + libdatachannel for `Gaming.Xbox.Scarlett` (reuse the proven
FetchContent recipe + the three interop fixes + `mbedtls_user_config.h`), then
run the existing `NEWTONIA_NET_SELFTEST` loopback and a real STUN/TURN connect
on the kit. The spike has **three** distinct questions, not one:
1. **P2P UDP + DTLS** (gameplay data channels) — **policy-clear**: the guidance
   explicitly sanctions a title-owned DTLS protocol and recommends a maintained
   third-party lib (§5a), so the only open question is whether **libjuice's UDP
   sockets** work under the Game OS. DTLS-library decision: build libdatachannel
   with its **OpenSSL** backend (the doc's named recommendation) vs keeping
   **MbedTLS** (equivalent maintained lib) — and commit to shipping the latest
   version + patching CVEs.
2. **Signaling WebSocket + TLS** — libdatachannel brings its **own WebSocket
   client + MbedTLS TLS**, but the NDA secure-comms guidance says titles should
   use **XCurl/libHttpClient** and *"must avoid other third-party HTTP/WebSocket
   client libraries,"* and the console sandbox **limits certificate-store
   access** (may break MbedTLS validation). So our WSS signaling is the shakier
   leg — if it fails, walk the **signaling fallback ladder (§5a-i)**: prefer a
   **libHttpClient/XCurl client to our *own* Worker** (keeps one pool, no PlayFab,
   web safe) before a PlayFab-Lobby broker.
3. **Relay reachability** (§5b) — a *relayed* TURN connect, not just LAN.
- **Pass (all three)** → flip `NEWTONIA_NET` ON for Scarlett; console joins the
  unified pool. Proceed to X3.
- **Partial** (e.g. UDP/DTLS OK, WSS signaling not) → keep WebRTC/DTLS transport,
  walk the signaling fallback ladder (§5a-i): libHttpClient→Worker first, then
  dual-broker/bridge, PlayFab Lobby last.
- **Fail** (P2P restricted / won't certify) → PlayFab Party backend becomes
  required; open the pool decision.

Exit: a definitive yes/no per leg (P2P, signaling, relay) on the console, logged.

### Phase X2 — non-NDA compliance seams (no hardware) — **items 1–4 LANDED**
All platform-neutral, all improve the existing shipping netplay, all give the
private Xbox backend a place to land — mirrors the `Achievements` / `Presence`
seam pattern.

> **Status (2026-07-20): items 1–4 below are implemented upstream** (branch `claude/net-protocol-identity-ki90s4`): identity on
> the wire as an append-only HELLO/WELCOME extension with NO `PROTO_VERSION`
> bump (`net_protocol.h`, `net_session.*`), the `net_identity.*` seam (default
> compile-time platform + "PLAYER"; backends implement
> `NetIdentityBackend::local_platform()/local_name()` — Steam persona under
> `STEAM_BUILD`, and the fork's gamertag backend defines
> `NEWTONIA_NET_IDENTITY_BACKEND` and returns `NET_PLATFORM_XBOX` in its own
> TU, no shared-layer edit), the badge UX (lobby "HOSTED BY <NAME> -
> <PLATFORM>", `Overlay::net_badges` bottom-row tags (peer + local), name-based
> DISCONNECTED/RECONNECTED — a legacy peer renders exactly the pre-badge UI),
> and the `net_policy.*` seam (default allow-all; the private backend defines
> `NEWTONIA_NET_POLICY_BACKEND` and supplies the `XUserCheckPrivilege` checks
> — multiplayer 254, cross-network 185, per the public GDK docs — from a
> CACHED snapshot, both calls are hot paths). `net_online_play_allowed` gates
> the menu ONLINE row + lobby commits; `net_comms_allowed_with` is enforced
> inside the NetSession handshake (host refuses pre-WELCOME with MSG_REJECT
> reason `RejectNotAllowed`; client refuses locally on the WELCOME identity),
> so a blocked peer gets an honest refusal on every adoption path, including
> mid-game rejoin. Guards: `test/e2e/identity.sh`, `test/e2e/identity_legacy.sh`
> (mixed-version interop, both directions). Items 8c/8d below remain open.
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
against our session service, or PlayFab Lobby); **authenticated signaling per the
secure-communication recommendations (§5a) — lean Path B: PlayFab Lobby+Identity
carries XSTS-authenticated signaling on the Xbox build while WebRTC/DTLS keeps
the transport**; XR-007 PDP disclosure; packaging + submission. Encryption is
already met (§5a); the work here is the authentication recommendation, calibrated
by the forum answer (§8a).

## 7. Code work-item list

The **Where** column records where each item's code *lives* (public repo vs
private mirror vs a portal), which is a different question from who owns the
work. Since the 2026-07-30 deferral **every open row is driven by the private
repo**, including the ones whose code is public-safe — those simply flow back
upstream as normal PRs (`xbox/PRIVATE_REPO.md`). Rows marked **landed** are
already in the public repo.

| # | Item | Files | Phase | Where the code lives |
|---|------|-------|-------|-------|
| 1 | Scarlett netplay build block (ready-to-enable, still OFF so console-smoke stays green) | `xbox/CMakeLists.txt` | X1 | Public |
| 2 | libdatachannel-on-console socket spike | build + dev kit | X1 | Private/hardware |
| 3 | Platform id on the wire (HELLO/WELCOME) — **landed** (append, no PROTO bump) | `net_protocol.h`, `net_session.*` | X2 | Public |
| 4 | `net_local_identity()` seam + default/Steam backends — **landed** | `net_identity.*`, `steam_identity.cpp` | X2 | Public |
| 5 | Platform badge UX (lobby + HUD) — **landed** | `net_lobby.cpp`, `view/overlay.*` | X2 | Public |
| 6 | `net_policy` seam (default allow-all) — **landed** | `net_policy.*` | X2 | Public |
| 7 | XUser sign-in + privilege checks | `xbox_main.cpp`, private `net_policy` backend | X4 | Private |
| 8 | Xbox-native invite/join (MPA or PlayFab Lobby) | private | X4 | Private |
| 8a | XSTS-authenticated signaling backend on Xbox (**Path B**: PlayFab Lobby+Identity; keeps WebRTC/DTLS transport) — §5a | `net_signal.*` Xbox backend, private | X4 | Private |
| 8a-A | **Path A** alternative (web-preserving, no PlayFab): our broker validates the XSTS token + attaches identity, keeping the room-code rendezvous on our Worker; adds per-platform validators (Steam/etc.) + relying-party private-key custody — §5a | `signal/src/worker.js`, `net_signal.*`, `net_local_identity` backends | X4 | Public (broker) + Partner Center (relying party) |
| 8a-L | Signaling fallback **rung 2**: libHttpClient/XCurl WebSocket client to our *own* Worker (console-legal `NetSignal` sibling; one pool, no PlayFab) — §5a-i | new `net_signal_gdk.cpp`, `NetSignal::create()` selector | X1/X3 | Private (GDK) |
| 8a-Br | Signaling fallback **rung 4**: PlayFab-Lobby↔Worker *signaling* bridge (opaque SDP/ICE mirror, normalizes TURN; not a gameplay relay) — only if Xbox is Lobby-only yet cross-network wanted — §5a-i, full strategy `xbox/NETPLAY_BRIDGE.md` | `signal/src/worker.js` + PlayFab server-API client | X4 | Public (bridge) |
| 8b | Default **relay-only** on the Xbox build for IP confidentiality (§5b) — reuse `set_force_relay` | xbox build/net config | X3 | Public (validate on kit) |
| 8c | Host-side sanity bounds on client-authoritative pose/fire (logic integrity, §5c) — clamp/discard implausible pose deltas | host handlers (`glgame.cpp` net apply, `net_session.*`) | X2/general | Public |
| 8d | Security-test the net protocol/parser with **malformed + invalid auth data** (fuzz `Net::Reader`, bad certs/tokens); extend the e2e set | `net_protocol.h`, `test/e2e/` | X2/general | Public |
| 9 | PlayFab Party `NetTransport` backend (only if X1 fails / cert requires) | new `net_transport_party.cpp` | X4 | Private |
| 10 | Store PDP cross-network disclosure | store listing | X4 | Partner Center |
| 11 | PII handling: badge uses display name (not XUID); no peer PII persisted (XR-014, §5) | `net_protocol.h`, `net_lobby.cpp`, `view/overlay.*` | X2 | Public |

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

### 8a. Microsoft's response (received — the big questions are answered)

- **(1) Foundation Mode:** confirmed — we have access at **no cost**.
- **(2) Our own stack:** **PlayFab is NOT required for any part of cross-network.**
  It's recommended (it's Microsoft's own solution), not mandated. Our WebRTC
  stack is acceptable.
- **Policy change — XR-134 retired.** The old, stricter requirement on *which*
  networking technologies a title could use has been **withdrawn** and replaced
  by guidance: *Best Practices for Secure Game Communication* / the
  *Communication Security Overview* (both NDA-gated on Learn). The standing
  obligation is to **secure all network communication regardless of content**
  (encryption; it extends even to P2P UDP QoS probes) — best-practice guidance,
  not a mandated transport.
- **We already satisfy the security bar by construction:** libdatachannel
  encrypts both data channels with **DTLS (via MbedTLS)** — all our traffic is
  already encrypted end-to-end. Confirm against the best-practices doc during
  Phase X4, but there is no known gap.
- **Next step Microsoft offered:** if we want written confirmation that our
  specific implementation is compliant, post on the **Xbox Developer Forums**
  and send them the link for their records. **Done — see §8b** (partial reply
  received 2026-07-15: the transport-basis question answered *yes*).

Net: question (2) — the biggest fork in §4 — resolves **in favor of keeping our
own stack**. PlayFab Party drops from "possible cert requirement" to "optional,
free, deferred fallback." (3) is moot unless we choose a custom Xbox-services
backend, which we are not.

### 8b. Xbox Developer Forums reply (partial — 2026-07-15)

We posted the compliance question §8a recommended. **Partial reply received; a
fuller answer is still being prepared.** The one part answered so far is the
biggest one:

> **Q:** With XR-134 retired, is a **title-owned WebRTC/DTLS peer-to-peer
> transport** an acceptable basis for cross-network co-op, provided we follow the
> secure-communication best practices?
> **A (from the experts they consulted): yes.**

So the §4 core decision / §8a question (2) is now confirmed **in writing on the
forums**, not just by email — the written-confirmation insurance §8a/§9 flagged
is (partially) in hand. Consequences:

- **Party is optional, never a cert mandate.** It remains only a *technical*
  fallback if the X1 spike fails — the "Party required → forces pool decision"
  risk (§9) is closed as a *compliance* pressure.
- **The whole B1 direction is validated** (our WebRTC + PlayFab-for-tokens, one
  pool, web preserved).

**Still open (not covered by this partial reply):**
- **How strictly the "secure-communication best practices" apply** — i.e. the
  *authentication* recommendation's strictness (encryption is already met by DTLS).
  This is exactly what §5a leans **B1** for and what the pending fuller answer
  should calibrate.
- **The joinable/invite rulings** — room-code-as-"joinable multiplayer"
  (XR-124/064) and Recent Player for XR-067 (`NETPLAY_CERT.md`) — a separate
  question for the ID@Xbox account manager, untouched here.

## 9. Risk register

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| libdatachannel P2P (UDP/DTLS) restricted on Game OS | Medium | **Policy-clear**: the guidance explicitly sanctions a title-owned DTLS protocol and recommends a third-party DTLS lib (§5a), so only libjuice **UDP-socket feasibility** remains for spike leg 1; Party is the fallback behind the same seam. (Down from Medium–High.) |
| Console signaling: third-party WebSocket lib discouraged + cert-store locked | Medium–High | NDA secure-comms guidance says use XCurl/libHttpClient and avoid third-party HTTP/WS libs; console sandbox limits cert-store access, which may break libdatachannel's own WSS/MbedTLS. Mitigation: walk the **signaling fallback ladder (§5a-i)** — prefer a libHttpClient client to our *own* Worker (one pool, web safe) over a PlayFab-Lobby broker; a signaling *bridge* pairs the two brokers if Xbox ends up Lobby-only. Keep WebRTC/DTLS for gameplay throughout. Spike leg 2 confirms |
| ~~Cert rejects a non-Party cross-network transport~~ | ~~Medium~~ → **Resolved (forum-confirmed)** | Microsoft confirmed our own stack is acceptable and retired XR-134 (§8a); a title-owned WebRTC/DTLS P2P transport was confirmed an acceptable basis for cross-network co-op **in writing on the Xbox Developer Forums** (§8b, 2026-07-15). Our DTLS traffic already meets the "secure all communication" best practice |
| Party required → forces pool decision | Low | No longer forced by cert (§8a/§8b); only relevant if the X1 *technical* spike fails. Seam keeps a Party backend additive |
| Secure-communication *recommendations* not fully followed | Low | The table is explicitly recommendations, not a gate (§5a). Encryption already met (DTLS + WSS/TLS); the open piece is the *authentication* recommendation (XSTS on signaling), closed in Phase X4 via **Path B** (PlayFab Lobby+Identity) or **Path A** (our own XSTS-validating broker, item 8a-A — web-preserving, cost: private-key custody + per-platform validators), calibrated by the forum answer |
| **Web players stranded** by an all-PlayFab path | Medium (drifts up if convenience wins) | Cross-network — incl. web — survives only while the Xbox build keeps our room-code rendezvous (§5a). PlayFab **Party** has no web SDK (§4a), and moving Xbox *rendezvous* to PlayFab **Lobby** can't pair with a Worker peer without a bridge. Mitigation: keep rendezvous on our Worker (gated on X1 leg 2); if auth is taken up, prefer **Path A** or layer PlayFab **Identity** *on top of* the Worker rendezvous rather than swapping it out; treat full Party migration as the explicitly-rejected option |
| Player IP exposure via ICE/STUN (confidentiality — guidance calls it "essential") | Medium | Default relay-only (TURN) on the Xbox build via existing `set_force_relay` (§5b); one-sided forcing hides both peers; relay reachability tested in the X1 spike. Cost: per-session TURN egress |
| Client-authoritative messages trusted without bounds (logic integrity) | Low | Message integrity already met by DTLS (§5c); host-authority bounds inputs by design; add host-side pose/fire sanity clamps (item 8c). Friendly co-op = low stakes |
| Console bring-up (X0) slips | High | Out of scope here; Phase X2 seams proceed on all existing platforms regardless |
| Snapshot bandwidth on console late-game | Low–Medium | Already have 5 Hz fallback + delta snapshots (NETPLAY.md M2) |
| No dev kit yet | High (today) | Phase X2 is fully buildable/testable without hardware; X1/X3/X4 staged for the kit |
