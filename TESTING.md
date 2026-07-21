# Testing Newtonia

Every test layer in the project, cheapest first. CLAUDE.md documents the
general headless-driver technique (Xvfb + xdotool + gdb); this file is the
inventory of what exists and how to run it.

## 1. Build gates

```sh
# Syntax-check one file without a full build (the pre-commit hook does this)
g++ -std=c++11 -fsyntax-only -I. -I/usr/include/SDL2 <file.cpp>

# Full builds — netplay is ON by default (needs ./build_netplay_deps.sh once);
# switching NETPLAY on/off needs a clean (objects don't mix)
make -j                                   # netplay build (the default)
make clean && make -j NETPLAY=0           # netless stub build
```

### STEAM_BUILD syntax gate (no SDK needed)

`STEAM_BUILD` code only compiles in the tag-triggered deploy-steam workflow,
so a typo in `steam_build.h` or its callers would otherwise surface at
release time. `test/steam_stub/` carries a minimal `steam/steam_api.h` with
signatures copied verbatim from the real SDK; syntax-check any file that
touches the Steam API against it:

```sh
g++ -std=c++11 -fsyntax-only -DSTEAM_BUILD -Itest/steam_stub -I. -I/usr/include/SDL2 \
    net_lobby.cpp menu.cpp steam_presence.cpp steam_invites.cpp steam_keyboard.cpp \
    steam_identity.cpp steam_identity_verify.cpp
```

When adding a Steamworks call: verify the signature against the SDK docs /
headers first, add it to the stub, then use it in game code.

## 2. In-binary selftests (headless, no display needed beyond Xvfb)

```sh
# WebRTC loopback: two transports in-process, offer/answer, reliable +
# unreliable round trip. No relay needed. This is the gate every native
# deploy job runs.
NEWTONIA_NET_SELFTEST=1 SDL_AUDIODRIVER=dummy xvfb-run -a ./newtonia
# -> "NET SELFTEST PASS" and exit

# Signal relay round trip: host+join sockets, room code, offer/answer
# relay against whatever NEWTONIA_SIGNAL_URL points at (default: the
# production worker; point it at a local wrangler dev to stay offline).
NEWTONIA_SIGNAL_SELFTEST=1 SDL_AUDIODRIVER=dummy xvfb-run -a ./newtonia
```

## 3. Signal worker tests (node, no game build)

The worker (`signal/`) has its own tests — see `signal/README.md`:

```sh
cd signal
npx wrangler dev --local --port 8787 &   # local relay (also used by the e2e drivers)
# If `wrangler@latest` (4.x) aborts with a workerd SQLite error
# ("table _cf_ALARM has 3 columns but 2 values"), its bundled runtime
# mismatches the persisted Durable Object state: clear it and pin an older
# wrangler — `rm -rf .wrangler && npx wrangler@3.114.1 dev --local --port 8787`
# (warns it caps the compat date to 2025-03-10; harmless for these tests).
node test/reclaim_test.js                # M3-1 protocol: token + grace + reclaim,
                                         #   incl. abrupt-drop reclaim past a stale
                                         #   host socket (ghost eviction)
node test/rate_key_test.mjs              # rate_key /64-collapse unit test
node test/pv_replay_test.mjs             # stored-offer replay keeps the version stamp
node test/steam_verify_test.mjs          # V1 Steam verifier, mocked Valve (unit)
# The identity protocol test needs the FAKE_VERIFY dev flag set on the relay:
#   npx wrangler dev --local --port 8787 --var FAKE_VERIFY:1
node test/identity_test.js               # V0 identity attest/broadcast/replay
```

Note on the Limiter: `wrangler dev --local` has no `CF-Connecting-IP`, so every
socket shares the rate-limit key `local` (HOST_LIMIT 10 / 10 min). Repeated
host-creates across many test runs can trip it (`[lobby] manual fallback:
rate-limited` in a game log) — `rm -rf signal/.wrangler` resets the persisted
Limiter DO between heavy runs.

`SIGNAL_WS=wss://... node test/pv_replay_test.mjs` points a test at another
relay (e.g. production after a worker deploy).

## 4. End-to-end drivers (`test/e2e/`)

Two-instance gameplay regressions under Xvfb: real windows, real input via
xdotool, real relay, assertions greped from `NEWTONIA_NET_DEBUG=1` logs.

```sh
sudo apt-get install -y xvfb xdotool x11-apps imagemagick   # once
cd signal && npx wrangler dev --local --port 8787 &         # relay
make clean && make -j                                       # netplay build (default)

test/e2e/room.sh     # connect via room code, 3 level skips, both fire 8s
test/e2e/lan.sh      # LAN play, NO relay: dead signal URL -> host beacons +
                     # manual fallback, joiner discovers on CodeEntry
                     # (loopback beacon), arrow-selects, blob exchange over
                     # TCP, host-candidate session to bootstrap. Uses a
                     # private NEWTONIA_LAN_PORT so parallel runs (or a
                     # real session) don't cross-beacon.
test/e2e/lanclip.sh  # the LAN-vs-clipboard race (one-box mac field bug): host
                     # reaches the manual fallback FIRST so its INVITE blob is
                     # on the shared clipboard when the joiner opens CodeEntry;
                     # asserts the auto blob pickup is HELD ("invite blob on
                     # clipboard held"), the LAN row appears, and the join runs
                     # over the LAN door anyway.
test/e2e/lankeep.sh  # LAN door + LIVE relay (needs the local wrangler +
                     # xclip): the room stays open when the LAN door wins
                     # the pairing - clears the clipboard to beat the code
                     # auto-join, pairs via the LAN row, then asserts BOTH
                     # rejoin doors open on peer loss and a re-pair lands.
test/e2e/lanrejoin.sh # LAN mid-game rejoin, both directions, no relay:
                     # SIGKILL the LAN joiner -> host reopens the LAN door
                     # (re-beacon + pause) and a NEW instance re-pairs in;
                     # then SIGKILL the host -> the client auto-browses for
                     # the host NAME and re-pairs into a freshly launched
                     # host's game (2nd bootstrap).
test/e2e/rejoin.sh   # SIGKILL joiner mid-game -> auto-pause -> rejoin -> resume
test/e2e/impacts.sh  # gen-3 spin-and-fire: joiner detects cosmetic impacts locally
test/e2e/ownroom.sh  # shared-prefs auto-join probe (mac host+client on one box)
test/e2e/mismatch.sh # fake pv-less old host (node) -> instant VERSION MISMATCH
test/e2e/hiccup.sh   # transport dies under live processes -> auto-pause -> AUTO-rejoin
test/e2e/turnexpiry.sh # REAL TURN expiry on a relay-forced pair (needs UDP egress)
test/e2e/spectate.sh # one player out of lives -> "SPECTATING IN N" -> camera to peer
test/e2e/spectate_disconnect.sh # joiner spectating -> host process killed -> GAME OVER
test/e2e/invite.sh   # host re-advertises the open slot on peer loss, clears on menu teardown
test/e2e/weapons_net.sh # PROTO 18: lance pulses + beam clones both ways (normal
                        # default netplay build; the driver sets the runtime hook
                        # NEWTONIA_NET_TEST_GRANT_WEAPONS=1 to stock both weapons.
                        # Online the hook must be set on the HOST — it grants
                        # both ships and replicates; on a client it is a no-op,
                        # since weapons are host-owned and a local grant would
                        # fight the snapshot restore)
test/e2e/revive.sh   # co-op revive: drop gating (partner out, 10%, one at a time)
                     # + the NEWTONIA_NET_TEST_REVIVE_MS payload hook -> the
                     # fallen joiner leaves spectate and respawns, no GAME OVER
test/e2e/gensoak.sh  # late-gen soak: host skips online to gen 25 (black hole,
                     # mini-station, gen-20 station/enemies, world growth) with
                     # per-gen liveness + no-drop + clean-log asserts
test/e2e/shock_net.sh # PROTO 22: Shock chain-lightning both ways. Both sides
                     # launch with NEWTONIA_ALL_WEAPONS=1, fire held, and must
                     # log "shock bolt received" (MSG_SHOCK) both directions with
                     # SANE counts (tens, not thousands — a runaway means an
                     # accumulator isn't cleared) and a clean log.
test/e2e/hazards_net.sh # Mid-game hazards online: host skips to gen 12 (past
                     # pulsar/comet/seeker); the joiner must reconcile all three
                     # kinds from the snapshot ("hazard replica spawned (kind
                     # 0/1/2)") plus a death burst, with a clean log.
test/e2e/shock_hazards_net.sh # PROTO 22 shock vs a hazard: host skips to gen 9
                     # (pulsar — non-lethal so the joiner keeps firing, and the
                     # survivor case that must stop() the arc). Both spin-fire
                     # shock (ALL_WEAPONS auto-selects it); asserts bolts round-
                     # trip both ways, the pulsar replicated, nobody crashed
                     # (the client now seeks hostiles + drains hazard/partner
                     # struck entries), clean log. Guards #142.
test/e2e/replay_playback.sh # REPLAY.md R2 exit criteria, solo (no relay):
                     # record a run, play it back (NEWTONIA_REPLAY_PLAY=
                     # current) — world unfolds (screenshots differ over
                     # time), pause survives, playback reaches the end and
                     # Esc exits; x4 speed finishes the file in under half
                     # real time; a 2-player recording grows the ghost
                     # roster at the recorded join ("replay: player 2
                     # joined") and plays back split-screen; a game-over
                     # recording reaches the GAME OVER card; an ALL_WEAPONS
                     # run round-trips the flash-class effects (REC_EFFECT:
                     # lance pulse, shock arc, nova ring — asserted via the
                     # record + receive log lines).
test/e2e/replay_menu.sh # REPLAY.md R3: the REPLAYS menu row (shown once a
                     # .nrp exists) opens the list screen; selecting
                     # CURRENT RUN starts playback ("replay: playback
                     # started"), Esc returns to the menu, and the list
                     # backs out cleanly on a second visit.
test/e2e/replay_online.sh # REPLAY.md online recording (needs the relay):
                     # host + client each record their session into their
                     # own pref dir's replays/online.nrp (per-instance
                     # XDG_DATA_HOME — one shared dir would interleave).
                     # Asserts both sides bank keyframes/deltas/effects
                     # after the pause checkpoint flush; the host records
                     # straight through a SIGKILLed peer (file grows while
                     # sends are skipped); the relaunched joiner's rejoin
                     # RESUMES its file ("replay: resuming recording" — the
                     # run_id seam rides the snapshots); clean abandons
                     # patch both headers (host marked 2P); both files
                     # play back (NEWTONIA_REPLAY_PLAY=online); and the
                     # REPLAYS menu's ONLINE RUN row starts the same
                     # playback (the 4th slot — online.nrp never rotates).
test/e2e/replay.sh   # REPLAY.md R1 exit criteria, solo (no relay needed):
                     # abandon leaves a resumable current.nrp; CONTINUE
                     # appends to the SAME file (one run_id, seam keyframe,
                     # continuous slots); NEW GAME rotates old runs into
                     # recent.nrp; clean higher scores promote best.nrp;
                     # cheat runs and crashed (stale-header) runs never do;
                     # game over patches the header (ENDED) and deletes the
                     # save. Headers/records parsed by test/e2e/replay_check.py.
test/e2e/identity.sh # peer-identity happy path: named exchange both ways
                     # (NEWTONIA_NET_NAME=GLENN/BOB — default builds send
                     # no name) logged as "net: identity peer name='GLENN'
                     # platform=DESKTOP(1)"; phase B is the badge-only
                     # state (NEWTONIA_NET_ANON_IDENTITY=1 host sends
                     # name_len 0 — platform known, name withheld, distinct
                     # from the legacy no-append case) and the receiver's
                     # role labels (PLAYER 1 = host, PLAYER 2 = client). Run
                     # against a PLAIN relay (no FAKE_VERIFY) — the claim must
                     # stay unattested so the display shows role labels.
test/e2e/identity_attested.sh # V0/V1 worker attestation: self-hosts its OWN
                     # FAKE_VERIFY relay (private port, so the shared :8787 dev
                     # relay is untouched), connects a named host+joiner, and
                     # asserts BOTH sides log "net: identity attested
                     # name='GLENN'/'BOB' platform=DESKTOP(1)" — the worker
                     # verified each side and the game folded it in as ATTESTED
test/e2e/identity_legacy.sh # mixed-version interop: a legacy peer (short
                     # HELLO/WELCOME via NEWTONIA_NET_NO_IDENTITY=1, both
                     # directions) must still handshake + bootstrap, with the
                     # current side logging "net: identity none (legacy peer)"
                     # — guards the append-only wire convention (no PROTO bump)
test/e2e/policy.sh   # net_policy refusal path: a host refusing all peers
                     # (NEWTONIA_NET_TEST_REFUSE_COMMS=1, default backend's
                     # inert hook) must reject INSIDE the handshake (MSG_REJECT
                     # RejectNotAllowed before WELCOME); the joiner never
                     # bootstraps and both sides stay alive
```

`NEWTONIA_TEST_SPAWN_PICKUPS=1` (offline, inert without the env var) rings
one of each pickup around the spawn with the ship parked at the centre —
screenshot it to eyeball the full icon set after touching pickup art.

spectate.sh uses the host-only `NEWTONIA_NET_TEST_KILL_MS`/`_WHO` hooks
(inert without the env vars) to empty a player's lives on a timer — lives are
host-authoritative, so it is applied on the host and replicates. `_WHO`
defaults to `remote` (the joiner spectates the host); pass `SPECTATE_WHO=local`
to make the host spectate the joiner. It burst-screenshots the spectator's
window across the 5 s countdown and the hand-off; asserts SPECTATE-E2E-OK and a
clean log.

turnexpiry.sh is the real-credential companion to hiccup.sh and CANNOT run
in the dev container (no UDP egress; STUN/TURN unreachable). Run it on any
Linux box with normal internet after `wrangler secret put TURN_TTL` (e.g.
90) + deploy; it forces both instances relay-only, asserts
"ice path relay/relay", waits out the expiry, and asserts the self-repair.
Manual equivalent on a mac: run two instances with
NEWTONIA_NET_FORCE_RELAY=1, check the debug overlay (B) shows
"net: relay/relay", play past the TTL, watch the auto-pause/rejoin heal.
Delete the TURN_TTL secret and redeploy afterwards.

### Simulating relay-path loss locally (no relay needed)

The dev container's kernel has no netem, but iptables works — real UDP
loss below SCTP, which reproduces the retransmit-stall dynamics a lossy
relay causes (root + sandbox override required):

```sh
/usr/sbin/iptables -A INPUT -i lo -p udp -m statistic --mode random --probability 0.02 -j DROP
test/e2e/room.sh          # or the gen-10 probe; read the reconcile summaries
/usr/sbin/iptables -D INPUT -i lo -p udp -m statistic --mode random --probability 0.02 -j DROP
```

Healthy signature under 2% loss: steady reconcile <10/s with small
maxes, loss bursts glide (0 snaps), no input gaps at loopback RTT
(retransmits recover inside the 300 ms gap-log threshold).

### One-phone relay test (no env vars)

Two instances on one device always pick the direct ICE path, so the
joiner must be relay-forced. Codes never contain `0`, so typing **`0` on
the join screen before the code** toggles a relay-only join (status:
"RELAY-ONLY JOIN ARMED (TEST)"). Works from every platform's code entry,
and the arming is process-wide so an auto-rejoin after an expiry kick
re-relays too. On one Android phone:

1. Put the Newtonia app and a browser in split-screen (both must stay
   foreground — a fully backgrounded host pauses and eventually freezes).
2. App: ONLINE → HOST. Browser: web build, ONLINE → JOIN. If the browser
   offers to paste/read the clipboard, decline — the auto-join would race
   the arming. Type `0`, then the code shown in the app.
3. Connected at all = relayed: the armed side DROPS every non-relay
   remote candidate (gathering-side filtering alone proved insufficient
   — libdatachannel still fired direct checks and once connected
   host/host), so the only reachable peer address is its TURN endpoint.
   The armed side logs "relay-only ICE policy ACTIVE" and one
   "dropped remote ... candidate" per filtered candidate. `npx wrangler
   tail` shows `turn creds minted, ttl=...` to confirm any TURN_TTL
   override took effect.

hiccup.sh simulates the mid-session transport death a TURN credential
expiry causes (SIGSTOP the joiner until the host's ICE fails, then thaw):
it proves the self-repair loop, but not the credentials themselves —
that needs real Cloudflare TURN, a tiny-TTL test key, and a relay-forced
pair, which no headless rig here can produce.

Each driver re-execs itself under `xvfb-run` when `DISPLAY` is unset, prints
`ROOM-E2E-OK` / `REJOIN-E2E-OK` on success, and exits non-zero on any
failure. Logs and screenshots land in a fresh temp dir (printed at start;
override with `NEWTONIA_TEST_OUT`). `XDG_DATA_HOME` is pointed into that dir
so the drivers never touch your real savegame or preferences.

### Writing a new driver

Source `test/e2e/lib.sh` and follow the room.sh shape. The load-bearing
rules (hard-won; the rest are in CLAUDE.md's headless-testing section):

- **Liveness after every input** (`alive $PID name`) — that is what
  pinpoints a crash to the step that caused it.
- **Assert on log markers, not on the driver reaching its last line** — a
  hung X client can get the tail of the script killed by the outer timeout
  while the run itself succeeded. Useful markers (`NEWTONIA_NET_DEBUG=1`):
  `[lobby] room <CODE>` (host room created), `bootstrap adopted` (joiner
  world up), `net: player 2 lost` / `net: player 2 rejoined` (rejoin flow),
  and `assert_clean` for the crash/corruption grep.
- **Menu navigation assumes fresh prefs** (lib.sh guarantees this): attract
  `Return`, then rows are NEW GAME / ONLINE — `s`,`Return` opens the lobby.
  A save file would add CONTINUE and shift the rows.
- **Skip-level (`n`) starts the next generation immediately** — no 5 s
  countdown; time your sleeps accordingly.
- Wrap ad-hoc runs in a hard `timeout` — a hung client can keep `xvfb-run`
  alive forever.

### Debugging a crash an e2e driver caught (core dump → backtrace)

When a driver reports `DEAD: joiner` (or host), don't guess — pull a real
backtrace. This is how the PROTO 22 shock crash was pinned to one line:

1. **Enable core dumps, then run the driver as-is.** The crashing instance
   drops a core in the process's cwd (the repo root, since `launch` runs
   `./newtonia` from `$ROOT`):
   ```sh
   echo 'core.%p' > /proc/sys/kernel/core_pattern   # pid-tagged, no clobber
   ( ulimit -c unlimited; timeout 200 bash test/e2e/shock_net.sh )
   ls core.*                                          # e.g. core.13852
   ```
2. **Backtrace it.** The binary must have symbols — build with `-g` (see the
   gotcha below):
   ```sh
   gdb -batch -ex "bt 30" -ex "info locals" ./newtonia core.13852
   ```
   `#0 __dynamic_cast … glgame.cpp:2935` pointed straight at a `dynamic_cast`
   on a freed pointer — a stale `Object*` left in a shock bolt's `struck`
   list after `net_apply_state` deleted the asteroid.

**Build gotchas that waste an hour if you miss them:**

- **Keep the real netplay define when overriding `CFLAGS` for symbols.** A
  command-line `CFLAGS=…` *replaces* the Makefile's flags — make cannot `+=`
  onto a command-line variable — so the netplay default's `-DNEWTONIA_NET_RTC`
  silently vanishes. Without it the menu's **ONLINE row disappears**, hosting
  never starts, and every driver dies with `NO ROOM CODE` that looks like
  flakiness. Always pass it back explicitly:
  ```sh
  make clean && make -j8 CFLAGS="-Wall -g -O2 -std=c++11 \
    $(sdl2-config --cflags) -MMD -MP -DNEWTONIA_NET_RTC -I$(pwd)/netplay-libs/include"
  ```
  (`LIBS` is `+=`-appended by the Makefile and survives, so only `CFLAGS`
  needs the manual define.) When a driver stalls on the menu, **screenshot
  it** (`xwd -id $W | convert … png`) — NEW GAME + OPTIONS with no ONLINE row
  is the unmistakable tell.
- **Use `-O2 -g`, not `-O0 -g`, for driver runs.** `-O0` is slow enough under
  Xvfb + software GL that the menu-nav keystrokes get dropped and the host
  never leaves the menu. `-O2` keeps a readable backtrace and stays fast
  enough for the driver's timing.

## 5. Debug knobs & getting them onto devices

The debug env vars (all inert unless set; the cheat-shaped ones flag the
game so achievements stay suppressed):

| Var | Effect |
|-----|--------|
| `NEWTONIA_BETA=1` | Unlocks dev-only features: `NEWTONIA_START_GENERATION=N` (new game starts at gen N) and the touch skip-level corner |
| `NEWTONIA_ALL_WEAPONS=1` | Full arsenal at 999 on every spawn, **Mine armed** as the secondary |
| `NEWTONIA_FRAME_LOG=1` | Logs every frame slower than 50 ms; on desktop the line carries `draws=` (shim/Mesh draw calls) and `segs=` (thick-line segments CPU-expanded) for that frame |
| `NEWTONIA_LINE_EMULATION=1` | Forces the thick-line quad emulation on platforms whose driver would draw wide lines natively (Android/iOS) — for A/B against the native path |
| `NEWTONIA_TEST_SPAWN_PICKUPS=1` | Pickup-icon ring (see above) |

Independent of any env var, the game SDL_Logs a **perf report** once per
second whenever fps drops below 55 —
`perf: fps=… tick=… draw=… objs=… stars=… osd=… lens=… other=… asteroids=… gen=…`
— sim vs draw, the draw sub-phases (game objects / starfield / HUD /
invisible-asteroid lens), and swap in `other`. This is the first thing to
capture for any "it got slow" report; it works on every platform:

- **Desktop**: run from a terminal.
- **Android**: `adb logcat -s SDL/APP` (filter on `perf:`).
- **iOS**: Xcode console.
- **Web**: the browser console — on a phone, attach remote DevTools
  (Android: `chrome://inspect/#devices` on a cabled desktop Chrome →
  *inspect* the tab; iOS: Safari's Develop menu).

### Env vars on Android (adb)

Android apps fork from zygote, so shell env never reaches them. Intent
extras named `NEWTONIA_*` are copied into the process env by
`NewtoniaActivity` instead:

```sh
adb shell am start -S -n org.newtonia/.NewtoniaActivity \
    --es NEWTONIA_BETA 1 --es NEWTONIA_START_GENERATION 9 --es NEWTONIA_ALL_WEAPONS 1
```

`-S` force-stops first — only a FRESH process reads the extras.

### Env vars on web (URL / localStorage)

`web_main.cpp` copies `NEWTONIA_*` URL query params — or, where the URL
isn't editable (the itch iframe), `localStorage` entries — into the env at
startup. Integer values only.

```
# directly-served build (e.g. the port-forward loop below):
http://localhost:8000/?NEWTONIA_BETA=1&NEWTONIA_START_GENERATION=9&NEWTONIA_ALL_WEAPONS=1

# itch: from the attached remote-DevTools console
localStorage.NEWTONIA_BETA = 1; localStorage.NEWTONIA_START_GENERATION = 9;
location.reload()          // localStorage.clear() to reset
```

Each applied knob logs `web: env NAME=VALUE (from URL/localStorage)`.

**Tight phone-web loop with no deploy** (needs emcc/tsc locally): `make
web`, `python3 -m http.server 8000 -d web/dist/play`, then in
`chrome://inspect` enable **Port forwarding** `8000 → localhost:8000`; the
phone's Chrome opens `http://localhost:8000` through the USB cable.

### Skipping levels on touch

The very top-right corner (x>0.85, y<0.15) is a skip-level tap on web and
Android — **only when `NEWTONIA_BETA` is set** (a working skip corner in a
normal game would let a stray tap cheat-flag the run). It synthesizes a
full n press+release, so it works on intro screens like the desktop key.
`adb shell input keyevent KEYCODE_N` works on Android regardless of the
gate.

## 6. What CI runs where

| Gate | Where |
|------|-------|
| Native build + `NEWTONIA_NET_SELFTEST` loopback | linux.yml, windows.yml on every push; every native deploy job |
| Xcode project compile with netplay vars (pbxproj regressions) | ios.yml |
| Web/Emscripten compile (only web-code gate — no emcc in dev containers) | web.yml |
| Xbox compile paths | xbox-dev.yml, xbox-console-smoke.yml |
| Worker unit + protocol tests (`signal/test/`) | deploy-signal.yml, as the deploy gate (prod on v* tags, beta worker on master pushes touching signal/) |
| Worker deploy | deploy-signal.yml (manual `npx wrangler deploy` still works — see signal/README.md) |

The e2e drivers are currently run locally/by-agent, not in CI (wrangler dev
+ Xvfb in Actions is possible if flakiness proves acceptable).

## 7. Hardware-only checks

Things no headless rig covers — verify on device after a `netplay-v*` tag:

- **Steam Deck**: floating keyboard pops on CodeEntry (Steam build only),
  stick feel in lobby/menu, picker as fallback.
- **iOS**: soft keyboard code entry, share sheet, background/resume rejoin.
- **Android**: back button paths, soft keyboard, background/resume rejoin.
- **Cross-play**: browser <-> native against the production relay and TURN
  (the local relay hands out no TURN credentials).
- Controller navigation generally — synthetic controller events can't be
  injected under Xvfb, so lobby/menu pad handling is verified by pattern
  (mirrors of proven menu code) plus on-device feel.
