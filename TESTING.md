# Testing Newtonia

Every test layer in the project, cheapest first. CLAUDE.md documents the
general headless-driver technique (Xvfb + xdotool + gdb); this file is the
inventory of what exists and how to run it.

## 1. Build gates

```sh
# Syntax-check one file without a full build (the pre-commit hook does this)
g++ -std=c++11 -fsyntax-only -I. -I/usr/include/SDL2 <file.cpp>

# Full builds — switching NETPLAY on/off needs a clean (objects don't mix)
make -j                                   # stub-net build
make clean && make -j NETPLAY=1           # netplay build (needs ./build_netplay_deps.sh once)
```

### STEAM_BUILD syntax gate (no SDK needed)

`STEAM_BUILD` code only compiles in the tag-triggered deploy-steam workflow,
so a typo in `steam_build.h` or its callers would otherwise surface at
release time. `test/steam_stub/` carries a minimal `steam/steam_api.h` with
signatures copied verbatim from the real SDK; syntax-check any file that
touches the Steam API against it:

```sh
g++ -std=c++11 -fsyntax-only -DSTEAM_BUILD -Itest/steam_stub -I. -I/usr/include/SDL2 net_lobby.cpp menu.cpp
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
node test/reclaim_test.js                # M3-1 protocol: token + grace + reclaim (24 checks)
node test/rate_key_test.mjs              # rate_key /64-collapse unit test
node test/pv_replay_test.mjs             # stored-offer replay keeps the version stamp
```

`SIGNAL_WS=wss://... node test/pv_replay_test.mjs` points a test at another
relay (e.g. production after a worker deploy).

## 4. End-to-end drivers (`test/e2e/`)

Two-instance gameplay regressions under Xvfb: real windows, real input via
xdotool, real relay, assertions greped from `NEWTONIA_NET_DEBUG=1` logs.

```sh
sudo apt-get install -y xvfb xdotool x11-apps imagemagick   # once
cd signal && npx wrangler dev --local --port 8787 &         # relay
make clean && make -j NETPLAY=1                             # netplay build

test/e2e/room.sh     # connect via room code, 3 level skips, both fire 8s
test/e2e/rejoin.sh   # SIGKILL joiner mid-game -> auto-pause -> rejoin -> resume
test/e2e/impacts.sh  # gen-3 spin-and-fire: joiner detects cosmetic impacts locally
test/e2e/ownroom.sh  # shared-prefs auto-join probe (mac host+client on one box)
test/e2e/mismatch.sh # fake pv-less old host (node) -> instant VERSION MISMATCH
test/e2e/hiccup.sh   # transport dies under live processes -> auto-pause -> AUTO-rejoin
test/e2e/turnexpiry.sh # REAL TURN expiry on a relay-forced pair (needs UDP egress)
test/e2e/spectate.sh # one player out of lives -> "SPECTATING IN N" -> camera to peer
test/e2e/spectate_disconnect.sh # joiner spectating -> host process killed -> GAME OVER
test/e2e/weapons_net.sh # PROTO 18: lance pulses + beam clones both ways (normal
                        # NETPLAY=1 build; the driver sets the runtime hook
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

## 5. What CI runs where

| Gate | Where |
|------|-------|
| Native build + `NEWTONIA_NET_SELFTEST` loopback | linux.yml, windows.yml on every push; every native deploy job |
| Xcode project compile with netplay vars (pbxproj regressions) | ios.yml |
| Web/Emscripten compile (only web-code gate — no emcc in dev containers) | web.yml |
| Xbox compile paths | xbox-dev.yml, xbox-console-smoke.yml |
| Worker deploy | manual `npx wrangler deploy` (see signal/README.md) |

The e2e drivers are currently run locally/by-agent, not in CI (wrangler dev
+ Xvfb in Actions is possible if flakiness proves acceptable).

## 6. Hardware-only checks

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
