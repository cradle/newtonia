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

### WebSocket TLS verification gate (`test/tls/`)
Not an in-binary selftest — a standalone binary against the same
libdatachannel prefix the game links, run by `linux.yml` on every push:

```sh
./test/tls/run.sh                 # or: ./test/tls/run.sh /path/to/netplay-libs
# -> tls: PASS
```

It mints a throwaway CA, stands up a local TLS WebSocket server, and checks
the three outcomes the signalling/leaderboard sockets depend on
(LEADERBOARD.md S1): the correct CA connects, an **unrelated CA is refused**,
and `disableTlsVerification` still connects (the `NEWTONIA_NET_TLS_INSECURE`
escape hatch). The middle case is the one that matters — if it ever starts
connecting, the platform credential a submit puts on the wire is readable by
anyone on the path. Needs `openssl(1)` and a prefix built WITH
`patches/libdatachannel-ws-ca-cert.patch` (an unpatched one cannot compile
the gate, or the game).

### Per-platform TLS pass — MANUAL, and not optional
The gate above runs on Linux against **OpenSSL**. That is one of two TLS
backends, and it is the one with a system trust store to fall back on:

| build | TLS backend | trust source | verified |
|-------|-------------|--------------|----------|
| Linux, `make` on macOS | OpenSSL | system store **+** our bundle | ✅ `linux.yml` gate |
| `make osx` (universal — the SHIPPED mac build) | MbedTLS | our bundle ONLY | ✅ 2026-08-03 |
| Android | MbedTLS | our bundle ONLY | ✅ 2026-08-04 |
| iOS | MbedTLS | our bundle ONLY | ✅ 2026-08-04 |
| Windows | OpenSSL | our bundle ONLY (OpenSSL cannot read the CryptoAPI store) | ✅ 2026-08-04 |
| Xbox | MbedTLS | our bundle ONLY | ⬜ — `cradle/newtonia-xbox` owns console runtime work |

So on four of those rows the carried roots are the *whole* trust story, and
before LEADERBOARD.md S1 none of them had ever completed a VERIFIED
handshake — verification was off everywhere. CI proves each platform still
COMPILES (an unpatched libdatachannel fails on the unknown field), not that
a real handshake succeeds. Run this once per platform after any change to
`net_ca_bundle.cpp`, `net_tls.cpp`, the patch, or a libdatachannel bump:

**Desktop** (`make`, `make osx`, the MSYS2 Windows build) — the selftest
hooks live in `glut.cpp`, so they exist here and nowhere else:

```sh
./build_netplay_deps.sh            # MUST be re-run: an old prefix has no
                                   # caCertificatePemFile and won't compile
make                               # or: make osx   /   MSYS2 make -j8
NEWTONIA_SIGNAL_SELFTEST=1 SDL_AUDIODRIVER=dummy ./newtonia
```

**iOS HAS the selftests** (`ios_main.mm`, under `NEWTONIA_NET_RTC`) — and
because each ends in `exit(ok ? 0 : 1)`, the process exit status IS the
verdict, so this works even when no log line reaches you:

```sh
xcrun devicectl list devices                    # grab the identifier
xcrun devicectl device process launch --console --terminate-existing \
  --environment-variables '{"NEWTONIA_SIGNAL_SELFTEST":"1"}' \
  --device <DEV> cc.gfm.Newtonia
#   -> devicectl reports the termination status: 0 = PASS, 1 = FAIL
```

(CI drives the same hooks on the SIMULATOR through `SIMCTL_CHILD_*` env
vars instead — see `ios.yml`.)

**Android has NO selftest hook** — `android_main.cpp` never reads those env
vars — so drive the real feature there, which is stronger evidence anyway:
a room code cannot appear unless the WSS handshake to the production worker
completed.

```sh
make android-install
adb logcat -c && adb logcat -s SDL/APP | grep -E "net: tls|net: signal"
#   ...then on the device: ONLINE -> HOST, and watch for a room code.
#   LEADERBOARD exercises the board socket the same way.
```

Note either way that `net_tls_log_state()` fires on the FIRST SOCKET, not at
startup: launching the app and filtering for `net:` shows nothing until you
actually reach ONLINE or LEADERBOARD.

**iOS needs the unified log, NOT a stdout stream.** SDL routes its log
through NSLog on Apple and is explicitly excluded from the `fprintf(stderr)`
fallback (`SDL_log.c` — the `__APPLE__ && (COCOA || UIKIT)` branch returns,
and the stdio block excludes the same condition), so
`devicectl … process launch --console` shows nothing. On macOS the lines
appear in a terminal only because NSLog falls back to stderr when one is
attached; a phone has none. Use Console.app (select the device in the
sidebar, filter `net:`) or:

```sh
brew install libimobiledevice
idevicesyslog | grep "net:"        # -u <udid> if several devices are attached
```

(`log stream --device` is gone from recent macOS — the `log` command no
longer talks to attached devices at all. If the Android tag filter comes up
empty, `adb logcat | grep "net:"` — the tag varies, see §5.)

Two things to confirm either way:
1. **The log line** — `net: tls - verifying server certificates against
   <path>`, emitted once by the first socket that opens. Anything else is a
   finding: `no CA bundle on disk` means the write failed (fatal on the
   MbedTLS rows above, silently UNVERIFIED on Windows), and `VERIFICATION
   DISABLED` means `NEWTONIA_NET_TLS_INSECURE` leaked into the environment.
2. **The connection actually completes** — a selftest PASS on desktop, a
   room code (or board rows) on mobile. Either way it is a real round trip
   against the production worker, exercising the actual Cloudflare chain
   through that platform's TLS stack; a verification failure shows up as a
   socket that never opens.

What a failure means, by platform: on MbedTLS builds the handshake fails
CLOSED (no online play at all — loud); on Windows libdatachannel falls back
to unverified when no CA is supplied, so a failed bundle write is SILENT
there and the log line is the only tell. MbedTLS is also the likelier place
for a surprise: it parses the bundle itself, and `mbedtls_x509_crt_parse_file`
SKIPS certificates it dislikes rather than failing (libdatachannel throws
only on a negative return), so a root could go missing with no error at all.

Why those ✅s settle the trust material: `VerifiedTlsTransport` sets
`MBEDTLS_SSL_VERIFY_REQUIRED` against our chain ALONE, so on an MbedTLS
build a handshake that completes at all proves the bundle wrote, MbedTLS
parsed it, and Cloudflare's real chain verified against the carried roots.
That has now happened on THREE MbedTLS platforms and three toolchains —
macOS universal (`SIGNAL SELFTEST PASS`, 2026-08-03), Android (bundle at
`/data/data/org.newtonia/files/cacert.pem`, exact expected size, then a
room code, 2026-08-04) and iOS (selftest against the production relay,
room `H8T7L`, 2026-08-04) — which retires the failure this section warns
about, a root silently skipped at parse time.

Windows was the one that mattered most and is also done (2026-08-04: bundle
at `C:\Users\…\AppData\Roaming\cc.gfm\newtonia\cacert.pem`, room
`Y8JZP`, `SIGNAL SELFTEST PASS`). It is the only platform upstream
libdatachannel refuses to verify on at all, so that run is the field proof
of the patch's SECOND hunk — and the proof is two-sided: the game compiles
only if `caCertificatePemFile` exists, and `git apply` is atomic, so a
Windows build that exists at all carries the `#ifdef _WIN32` relaxation too.
It is also the only row that falls back to UNVERIFIED rather than failing
closed, which is why the log line is the thing to re-check there after any
libdatachannel bump: a silent regression looks exactly like success.

Xbox shares the MbedTLS trust path proven three times above, and console
runtime work belongs to the private repo (CLAUDE.md) — the canaries here
only prove it still compiles.

The deploy jobs currently gate on `NEWTONIA_NET_SELFTEST` — an in-process
loopback that involves no TLS whatever. Adding `NEWTONIA_SIGNAL_SELFTEST`
there would automate this pass; deferred for now (it makes release builds
depend on the production worker being reachable), so until then the manual
pass above is the only coverage the MbedTLS platforms get.

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
node test/play_games_verify_test.mjs     # V2 Play Games verifier, mocked Google (unit)
node test/game_center_verify_test.mjs    # V3 Game Center verifier, real RSA + synthetic
                                         #   Apple cert, mocked fetch (unit)
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

The **leaderboard worker** (`board/`, LEADERBOARD.md) mirrors the pattern —
see `board/README.md`:

```sh
cd board
node test/validate_test.mjs        # .nrp header/record-framing validation (unit)
node test/identity_gate_test.mjs   # attestation admission gate (unit)
# Protocol test against the real worker under miniflare (local D1/R2):
npx wrangler@4 dev --local --port 8788 --var FAKE_VERIFY:1 --var SUBMIT_LIMIT:100 &
node test/board_test.mjs           # submit/supersede/dedup/fetch round-trip
```

Both suites gate `deploy-board.yml`. `SUBMIT_LIMIT` widens the per-IP
submit window for the test's burst; like `FAKE_VERIFY`, never set it in
production.

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
test/e2e/lan_hidden.sh # LAN-visibility opt-out: preseeds lan_visible=0 in the
                     # run's INI, drives the same host/join flow as lan.sh, and
                     # asserts the host NEVER beacons ("lan announce up" absent)
                     # and the joiner discovers nothing ("lan host found"
                     # absent) - the host still reaches the manual fallback, so
                     # the missing beacon is the pref, not a stalled flow.
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
test/e2e/lanrename.sh # LAN rejoin vs a RENAMED host (NEWTONIA_DEVICE_NAME
                     # override): the client browses for the old name, a
                     # fresh host beacons a different one -> auto-rejoin
                     # must NOT fire, the rejoin wait screen lists the
                     # live rows, and a manual Down+Enter joins it.
test/e2e/byecard.sh  # terminal disconnect card answers like a menu, no relay:
                     # host leaves to the menu (a deliberate BYE, so the
                     # joiner has nothing to rejoin) -> the joiner's card
                     # must survive movement keys (it used to take ANY key
                     # as "exit") and leave only on confirm.
test/e2e/rejoin.sh   # SIGKILL joiner mid-game -> auto-pause -> rejoin -> resume
test/e2e/rejoinexit.sh # the auto-rejoin wait screen answers like a menu:
                     # SIGKILL the HOST -> joiner's rejoin lobby -> its
                     # cursored BACK TO MENU row leaves on Enter (round 1)
                     # and Esc (round 2); per-round fresh prefs.
test/e2e/hostresume.sh # host process-death resume: SIGKILL the HOST mid-game,
                     # relaunch within the reclaim grace, drive the menu's
                     # RESUME HOSTING row -> room reclaimed, client auto-rejoin
                     # reconnects, generation survives via the online save;
                     # also guards the paused-unpause RX-watchdog fix and
                     # asserts quit-to-menu deletes the ticket + online save.
                     # Per-instance XDG_DATA_HOME (the relaunched host must
                     # find ITS ticket; the joiner must never see one).
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
test/e2e/missile_net.sh # client-fired deploys (host launches with
                     # NEWTONIA_ALL_WEAPONS=1 — weapons are host-owned, so its
                     # grant stocks both ships). Two assertions, both A/B'd:
                     # (1) NOT ONE missile vanished having flown under 200 ms —
                     # a missile that never flew cannot have hit anything, so
                     # that is the muzzle blast (Ship::NET_DEPLOY_GRACE; 9 of
                     # them with the hold disabled). (2) NO launch aged out
                     # unmade ("deploy dropped") — the final phase SIGSTOPs the
                     # joiner so three presses pile up in X and arrive in ONE
                     # INPUT, the shape a lost-packet stall delivers on
                     # recovery; 12 of 18 were lost before the host queued
                     # secondary presses like it queues primary ones.
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
                     # record + receive log lines); and a recording that
                     # ends mid-hum leaves the REPLAY ENDED screen silent —
                     # S7 plays back under SDL_AUDIODRIVER=disk and checks
                     # the mixer's own output (loud before the end-of-file
                     # mark, digital silence after it), the only assertion
                     # here that can see a stuck sound loop at all.
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
                     # Does NOT exercise the opening-keyframe drop path
                     # (REPLAY.md, 2026-07-27): nothing here fires inside
                     # the host's first 100 ms, so the recorder's
                     # "holding records until the opening keyframe" trace
                     # never appears here. It cannot: the game forces a
                     # keyframe as its first record either way (see the
                     # keyframe driver below, which covers that window
                     # directly).
test/e2e/replay_keyframe.sh # REPLAY.md keyframe ordering, via the in-binary
                     # recorder selftest (NEWTONIA_REPLAY_SELFTEST=1 — no
                     # relay, no display, no game). The ONLY coverage of the
                     # pre-keyframe hold: records offered before the opening
                     # keyframe are held, not written and not answered with a
                     # locally-built stand-in, and the count resets per seam
                     # (await_keyframe re-arms it on a client rejoin). Asserts
                     # the recorded file is K D D K D, both trace lines with
                     # their per-seam counts (3 then 2, never a cumulative 5),
                     # and that the throwaway selftest.nrp is cleaned up
                     # without touching the four real slots. Mutation-checked
                     # both ways (2026-07-30): deleting the hold yields
                     # [DKDDDDKD] — a delta before its own baseline, the field
                     # bug's exact shape — and deleting the reset reproduces
                     # the "5 record(s) held out" misreport. Also a linux.yml
                     # build gate.
test/e2e/replay.sh   # REPLAY.md R1 exit criteria, solo (no relay needed):
                     # abandon leaves a resumable current.nrp; CONTINUE
                     # appends to the SAME file (one run_id, seam keyframe,
                     # continuous slots); NEW GAME rotates old runs into
                     # recent.nrp; clean higher scores promote best.nrp;
                     # cheat runs and crashed (stale-header) runs never do;
                     # game over patches the header (ENDED) and deletes the
                     # save. Headers/records parsed by test/e2e/replay_check.py.
test/e2e/leaderboard.sh # LEADERBOARD.md L2 exit criteria (solo; starts a
                     # local board worker itself unless NEWTONIA_BOARD_URL
                     # points at one — needs node/npx): a clean personal
                     # best + game over -> qualify -> the UPLOAD TO
                     # LEADERBOARD? prompt -> YES uploads best.nrp, the
                     # worker places it and the row reads back with the
                     # header's exact score; a dead worker degrades
                     # silently (no prompt, no error card); a cheat-only
                     # run produces no board traffic, and
                     # leaderboard_prompts=0 AUTO-uploads without asking
                     # (the setting is ask-vs-auto); S5 forces an "unverified" first
                     # submit (a dedicated worker with REJECT_FIRST_VERIFY)
                     # and asserts the client warms a fresh credential,
                     # retries and places (credential-lifecycle hardening).
                     # The scoring spray retries until the run actually
                     # scores (a fresh world spawns asteroids clear of the
                     # ship).
test/e2e/replay_failures.sh # the recorder/reader paths that only run once
                     # something has already gone wrong (solo, no relay), each
                     # a bug that shipped. S1: a resume whose leftover ends in
                     # a TRUNCATED record trims the stub first — "ab" appends
                     # BEHIND the break, where the reader never arrives, so the
                     # whole resumed session used to vanish while the header
                     # patch advertised it (asserts the record count grows and
                     # playback reaches the far segment). S2: a real SHORT
                     # write — a 64 KB tmpfs filled by ordinary play, so fwrite
                     # writes part of a chunk and fails — must stop the
                     # recording, trim back to the last intact boundary and
                     # leave a playable file (skips where mount is
                     # unavailable). S3: a HOSTILE file is declined, not fatal:
                     # a 69-byte keyframe payload claiming 0xFFFFFFF0 weapons
                     # reached vector::resize and aborted the process, and the
                     # same parse runs on a peer's netplay snapshot — the
                     # assertion is the exit code (134 = SIGABRT) plus the
                     # "unparseable - declining" line.
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
                     # Phase C: accented names (BJÖRN/RENÉE) must fold to their
                     # ASCII base (BJORN/RENEE) over the wire — Tier-1
                     # transliteration in net_sanitize_name.
test/e2e/identity_attested.sh # V0/V1 worker attestation: self-hosts its OWN
                     # FAKE_VERIFY relay (private port, so the shared :8787 dev
                     # relay is untouched), connects a named host+joiner, and
                     # asserts BOTH sides log "net: identity attested
                     # name='GLENN'/'BOB' platform=DESKTOP(1)" — the worker
                     # verified each side and the game folded it in as ATTESTED
test/e2e/identity_tick.sh # the VERIFIED TICK on an attested badge (the V0
                     # polish item). Same FAKE_VERIFY pairing as
                     # identity_attested.sh, but goes on to capture what the
                     # PLAYER sees: the in-game HUD badge. ASSERTS the
                     # attestation logs, that both badge bands contain ink (a
                     # blank band = the HUD or badge regressed) and that both
                     # games survive the capture; the tick's SHAPE is left to
                     # the two PNGs in $OUT — pixel-geometry checks would be
                     # brittle across layout changes. Expect "BOB - DESKTOP"
                     # (host) and "GLENN - DESKTOP" (joiner), each with a
                     # checkmark after it. The only driver that checks a badge
                     # visually rather than by log line
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
| `NEWTONIA_REPLAY_ENABLE=1` | Force replay recording ON (it ships opt-in / default OFF — `Preferences::auto_record_replays`). The replay e2e drivers set this; `NEWTONIA_REPLAY_DISABLE=1` forces OFF and wins |
| `NEWTONIA_REPLAY_PLAY=<current\|recent\|online\|best\|last\|path>` | Boot straight into playback of that replay file (dev entry for R2; the REPLAYS menu is the real path) |
| `NEWTONIA_REPLAY_SELFTEST=1` | Run the recorder's keyframe-ordering selftest and exit 0/1 (`replay_selftest.cpp`), before any window or GL — the same hidden-hook shape as `NEWTONIA_NET_SELFTEST`, but present in netless builds too since replays are a solo feature. Driver: `test/e2e/replay_keyframe.sh` |
| `NEWTONIA_SAFE_INSET_TOP=N` | Forces a top display-cutout inset of N px, so the notch HUD layout (LEVEL/score/weapons shifted below the camera) is testable without cutout hardware. The real inset comes from `NewtoniaActivity`'s `DisplayCutout` on Android |

Independent of any env var, the game SDL_Logs a **perf report** once per
second whenever fps drops below 55 —
`perf: fps=… tick=… draw=… objs=… stars=… osd=… lens=… other=… asteroids=… gen=…`
— sim vs draw, the draw sub-phases (game objects / starfield / HUD /
invisible-asteroid lens), and swap in `other`. This is the first thing to
capture for any "it got slow" report; it works on every platform:

- **Desktop**: run from a terminal.
- **Android**: `adb logcat -s SDL/APP` (filter on `perf:`).
- **iOS**: Xcode console.
**Worked example — the late-generation collapse (2026-07-28).** A Moto G05
at generation 25 ran at 3-6 fps with `tick≈1070ms(max 269)` and
`draw≈25ms`: the split named the simulation immediately, and `max` (one
physics step) mattered more than the accumulated second. Cause was
`elastic_asteroid_collisions` testing every asteroid pair every step —
51,360 pairs at 321 asteroids, ~125 steps/s — to service the dozen
reflective rocks that generation has. Grid broad-phase fixed it (see
`Grid::query_neighbours`); the same device then reached generation 48 with
621 asteroids at 26-45 fps and `max` down to 16-51 ms. Two lessons for the
next report of this shape: **read `max`, not just the total** (a 10x win
showed there most clearly), and **suspect an all-pairs scan** whenever
`tick` grows faster than the object count.

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

### Running on an emulator or device (+ the Play Games attestation smoke test)

**Get a target attached** (`adb devices` should list it):
- *Physical device*: Developer options → USB debugging, plug in, accept the RSA prompt.
- *Emulator*: create an AVD from a **Google Play** system image. Play Games
  sign-in and server-side access need Google Play Services, so a plain
  "Google APIs" / AOSP image can **not** exercise identity attestation (it can
  still run the game and LAN/local play):
  ```sh
  sdkmanager "platform-tools" "emulator" "system-images;android-34;google_apis_playstore;x86_64"
  avdmanager create avd -n newtonia -k "system-images;android-34;google_apis_playstore;x86_64" -d pixel_6
  emulator -avd newtonia          # or Android Studio → Device Manager
  ```
  Then sign into a Google account inside the emulator (Settings → Passwords &
  accounts) so Play Games can authenticate.

**Build + install** (full toolchain — NDK + SDL2/SDL2_mixer siblings — per the
`### Android` build section in CLAUDE.md):
```sh
make android-install            # build the debug APK + adb install onto the attached target
# or install a CI artifact instead: adb install -r app-debug.apk   (from an android.yml run)
```

**Register the debug keystore's SHA-1 for a locally-built APK.** Play Games
matches *package name + signing SHA-1* against a registered OAuth Android
client, and only the **release / Play App Signing** SHA-1 is registered by
default. A locally-built APK is **debug-signed** (a different SHA-1), so Play
Games sign-in fails with `DEVELOPER_ERROR` ("Play Games not signed in — earns
held in memory" in logcat), and with no sign-in the phone fetches no name and
mints no server auth code → it sends an empty credential and the worker logs
`identity … platform=5 verified=false`. Fix: in **Google Cloud Console → APIs &
Services → Credentials → Create credentials → OAuth client ID → Android** (the
game's project — `717199808901`), add package `org.newtonia` + the debug SHA-1,
and make sure the test account is on the Play Games Services **testers** list.
The failing `SignInAuthenticator` logcat dump prints the exact SHA-1 to
register; or `keytool -list -v -keystore ~/.android/debug.keystore -alias
androiddebugkey -storepass android`. Play Store builds are unaffected (their
Play App Signing SHA-1 is already registered) — this is only for local/debug
installs. Give it a few minutes to propagate, then force-stop + relaunch.

**Play Games identity attestation smoke test** (NETPLAY.md V2). Prereqs: a
signal worker with the OAuth secrets set (`signal/README.md` — the beta worker
has them once master's `signal/` deploy has run, and Play Games needs BOTH
`PLAY_GAMES_OAUTH_CLIENT_ID`/`_SECRET` set with `--env beta`, not just the Steam
key), the debug SHA-1 registered (above), and **two** Google accounts (two
devices, or two accounts on one device). Launch each side pointed at the
beta relay with net debug on — Android delivers env via intent extras, and
`net:` lines are gated on `NEWTONIA_NET_DEBUG` (`-S` forces a fresh process so
the extras are read; see §5):
```sh
adb shell am start -S -n org.newtonia/.NewtoniaActivity \
    --es NEWTONIA_NET_DEBUG 1 \
    --es NEWTONIA_SIGNAL_URL wss://newtonia-signal-beta.gfmcc.workers.dev/ws
adb logcat -s SDL/APP | grep "net: identity"     # fallback if the tag differs: adb logcat | grep "net:"
```
HOST on one side, JOIN with the room code on the other. **Pass** = each side
logs the peer's *attested* identity and the lobby "HOSTED BY" / badge shows the
real name (not "PLAYER 1/2"):
```
net: identity attested name='<real Play Games name>' platform=ANDROID(5)
```
Negative cases degrade gracefully to the role label (never a crash): a build/
worker without the OAuth secret, a plain-AOSP emulator, or a not-signed-in
account all stay `net: identity` (claim) without the `attested` line.

**Last run: PASSED 2026-07-26**, alongside the equivalent Steam two-account
smoke (NETPLAY.md V1 §5.5). Both verifiers are live-verified; re-run this
procedure after any change to the credential mint, the worker's
`attest_identity` dispatch, or the per-platform verify modules.

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

### Replay recorder overhead on low-end Android (REPLAY.md open item)

The headless e2e prove the recorder is *correct*; only a real low-end phone
proves it's *cheap enough* — the last open item in REPLAY.md (mobile
overhead). One phone can't cover it: the recorder has three cost axes and no
single sub-$200 handset stresses all three. Two cheap devices between them do
(both bought 2026-07; add newer equivalents as they replace these):

| Device | SoC | RAM | Storage | Stresses |
|--------|-----|-----|---------|----------|
| **Moto E14** | Unisoc T606 (weakest current) | 2 GB, Android 14 **Go** | UFS 2.2 | CPU (10 Hz snapshot build) + RAM (buffer + whole-file Reader) + **lifecycle** (Go kills backgrounded apps hardest) |
| **Moto G05** | Helio G81 | 4 GB | **eMMC 5.1** | **storage-flush I/O** (the checkpoint appends — small slow writes) |

Counterintuitively the *cheaper* E14 has the *faster* storage (UFS), so it
does NOT exercise the flush-latency worry — the eMMC G05 is the only one that
does. Get the eMMC device for storage, the 2 GB Go device for everything else.

**Method (both devices).** Isolate the recorder's cost with an A/B — its
`perf: fps` contribution is the delta between recording on and off:

```sh
# Late-generation load (100+ asteroids is the worst case); KEYCODE_N marches
# levels fast (works on the intro screens too). NEWTONIA_* ride intent extras
# (see "Env vars on Android"); -S forces a fresh process that reads them.
# Recording ships opt-in (default OFF), so the ON run must force it on.
adb shell am start -S -n org.newtonia/.NewtoniaActivity \
    --es NEWTONIA_BETA 1 --es NEWTONIA_START_GENERATION 9 --es NEWTONIA_REPLAY_ENABLE 1
adb shell input keyevent KEYCODE_N        # ... march to a dense generation, play
adb logcat -s SDL/APP | grep "perf:"      # capture the perf lines

adb shell am start -S -n org.newtonia/.NewtoniaActivity \
    --es NEWTONIA_BETA 1 --es NEWTONIA_START_GENERATION 9      # recording OFF (default)
# ... same play, same capture. The fps/tick delta vs the first run = recorder cost.
```

**No `perf:` lines is the pass, not a broken capture** — the report is gated
on the second's frame count falling under 55 fps (`GLGame::perf_report`), so
a fast device is silent. Two things do have to be ruled out before banking
that silence: that recording was actually on (grep the same `SDL/APP` tag for
`replay: recording started (run_id=…)`, or `adb shell run-as org.newtonia ls
-l files/replays/`), and that the extras were read at all (`-S`, and a
`NEWTONIA_START_GENERATION` that visibly lands proves the whole extras path).
If the ON run is silent the A/B is already decided — the OFF run can at best
also be silent, so the delta cannot exceed the threshold. Note too that
`perf_report` restarts its window on any frame gap over 500 ms (intros, state
handoffs, backgrounding), which is what keeps bogus `fps=0` lines out; it
can also swallow a hitch landing exactly on a level boundary, so the
subjective "no perceptible hitch" check still earns its place below.

**Per-device focus:**
- **E14 (durability / onPause budget):** background the app mid-run, then kill
  it from the task switcher; relaunch and confirm the replay plays back intact
  (`NEWTONIA_REPLAY_PLAY=last`, or the REPLAYS menu). Android Go suspends/kills
  most aggressively, so if the checkpoint flush fits *its* onPause window it
  fits everywhere. (This is the same guarantee the Xbox suspend-budget rule
  needs — REPLAY.md.) **Know what a killed file looks like before judging
  it:** `finalize()` never runs, so the header keeps its initial score,
  generation and duration and `FLAG_CLEAN` stays unset — expected, not a
  failure, and it gates only best-promotion (`maybe_promote_best`), never
  playback. A truncated final record is likewise a designed-for crash
  artifact the reader's walk simply stops at. So "intact" means playable up
  to roughly the moment of backgrounding: losing the seconds after the last
  flush is a pass, losing the run is the failure. Play it back *before*
  starting anything else — `NEWTONIA_REPLAY_PLAY=last` resolves to
  `current.nrp` while its header reads, but a new game rotates that to
  `recent.nrp` (and the zero-tick rule can delete it outright if the new
  game records nothing).
- **G05 (flush latency):** watch for a frame hitch landing exactly on a
  checkpoint flush — level clear, pause, focus loss — where a slow eMMC write
  would show. The bounded-append design means each flush is ~one level of
  records; the eMMC G05 is where that bound gets its on-device proof.

**Pass:** no perceptible hitch through play or at level boundaries, the on/off
`perf` delta stays below the ~55 fps report threshold (recorder below the
noise floor), and the E14 replay survives a switcher-kill. Green on both
closes the REPLAY.md mobile-overhead question on real low-end hardware. The
first field pass (2026-07-20, a mid-range Android) already came back clean;
these two devices extend it to the low end.

**E14: PASSED 2026-07-27.** Generation 13 with recording forced on — live,
not assumed: the file was watched playing back afterwards — logged **no
`perf:` lines at all**, i.e. it held ≥55 fps throughout. Per the note above
that also settles the A/B on its own, so the recording-off control run was
skipped as a formality. Durability green in the same session: force-quit
mid-run, and the replay played back after relaunch, so the background flush
fits Android Go's `onPause` window — the tightest one going. CPU, RAM and
lifecycle are therefore all confirmed on the weakest current SoC.
**G05: PASSED 2026-07-28**, closing the storage axis and with it the
REPLAY.md mobile-overhead question. No `perf:` lines through play or death
at generation 9 (recording confirmed on — a replay was watched back), the
replay survived a force-quit, and repeated background/resume cycles stayed
clean. On flush latency specifically, which is the only axis the E14's UFS
2.2 could not reach: ~50 level boundaries marched back to back with no
perceptible hitch, and — the case that actually matters — a normally
played level of ~3.6 minutes (2614 records banked over 2149 slots, ~100x
what a two-second level skip produces) cleared with no hitch at the
boundary. Rapid skipping
alone would NOT have proved this: it banks the smallest possible chunk, so
it tests flush frequency rather than flush size.

Two notes for anyone repeating it. A live recording reads `duration_ms=0`
— the header's tail is patched only at a clean stop, and the reader is
built to tolerate a header staler than the records behind it. Judge chunk
size by `records`, but note records EXCEED slots — events and effects
attach to the upcoming slot without advancing it, so 2614 records spanned
2149 slots here (~1.2x). `duration_ms` after a clean stop is the honest
play-time figure; the record count is the honest write-volume one. And
the perf logger cannot answer this on its own: its window restarts on any
frame gap over 500 ms, so a hitch landing exactly on a level boundary can
fall into a discarded window — the subjective check is load-bearing here,
not a formality.

## 8. Web build in a dev container (emcc + a real browser)

The web target was long the one platform a container could not build —
`web.yml` was "the only emcc gate". It can, with two workarounds for the
agent proxy. Worth the ten minutes whenever a change touches
`web_main.cpp`, IDBFS persistence, or anything whose failure mode is
browser-shaped.

### Install the SDK

```sh
git clone --depth 1 https://github.com/emscripten-core/emsdk.git ~/emsdk
cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest
source ~/emsdk/emsdk_env.sh          # every shell that runs `make web`
```

### Work around the blocked port archives

`emcc` fetches SDL2/SDL2_mixer as GitHub **archive zips**, and the proxy
403s `github.com/*/archive/*` and `codeload.github.com` alike (plain `git`
and `raw.githubusercontent.com` are fine — that asymmetry is the whole
trick). Clone the ports at the exact tags the SDK asks for instead:

```sh
mkdir -p ~/ports && cd ~/ports
# tags come from upstream/emscripten/tools/ports/{sdl2,sdl2_mixer}.py
git clone -q --depth 1 -b release-2.32.10 https://github.com/libsdl-org/SDL.git       SDL-release-2.32.10
git clone -q --depth 1 -b release-2.8.0   https://github.com/libsdl-org/SDL_mixer.git SDL_mixer-release-2.8.0
```

Then one build with `EMCC_LOCAL_PORTS` to unpack them into the SDK cache:

```sh
export EMCC_LOCAL_PORTS="sdl2=$HOME/ports/SDL-release-2.32.10,sdl2_mixer=$HOME/ports/SDL_mixer-release-2.8.0"
make web
```

Two snags, both in the SDK rather than this repo, both one-line fixes to
`~/emsdk/upstream/emscripten/tools/ports/`:

- `sdl2_mixer.py` has no `SUBDIR`, which `EMCC_LOCAL_PORTS` requires —
  add `SUBDIR = 'SDL_mixer-' + TAG` next to `TAG`.
- With `EMCC_LOCAL_PORTS` still set, the parallel compile subprocesses each
  try to re-unpack the port and trip an `EM_CACHE_IS_LOCKED` assert. Once
  `cache/ports/{sdl2,sdl2_mixer}/` are populated, **unset it** and make the
  two `get()` functions skip their `ports.fetch_project(...)` call — the
  sources are already there, and the fetch is the only thing the proxy
  blocks. (mpg123 downloads fine; only the GitHub archives are refused.)

`make web` then works normally, and stays working — the cache persists.

### Drive it in a browser

Chromium is pre-installed (`/opt/pw-browsers`), but the npm `playwright`
package usually wants a newer build number than the image ships, so pass
the binary explicitly rather than running `playwright install`:

```sh
npm install playwright --no-save           # AT THE REPO ROOT (node_modules
                                           # is gitignored): these are ESM
                                           # drivers, so `import 'playwright'`
                                           # resolves from the SCRIPT's
                                           # directory upward — installing it
                                           # elsewhere and cd-ing there does
                                           # not work, and NODE_PATH is
                                           # CommonJS-only
python3 -m http.server 8099 --bind 127.0.0.1 -d web/dist/play &
CHROME=/opt/pw-browsers/chromium-1194/chrome-linux/chrome \
  node test/e2e/web_replay_tabkill.mjs     # tab-close durability
CHROME=/opt/pw-browsers/chromium-1194/chrome-linux/chrome \
  node test/e2e/web_replay_promote.mjs     # best-promotion regression
```

`web_replay_promote.mjs` guards a bug worth remembering: `copy_file` used a
64 KB stack buffer, and emscripten's default stack is 64 KB, so promoting a
run to `best.nrp` aborted the module. It hid because promotion skips
cheat-flagged runs and EVERY skip-level soak run is cheat-flagged — the
driver therefore plays a clean run with no skips. Build with
`EMCC_CFLAGS="-sASSERTIONS=2 -sSAFE_HEAP=1 -g2"` when chasing a trap: it
turns `index out of bounds` at some wasm offset into
`Aborted(stack overflow ... stack limits [0x00065da0 - 0x00075da0])`.

`launchPersistentContext(profileDir)` is what makes a returning player
testable: IDBFS lives in IndexedDB, so a fresh `newPage` on the same
profile is the same save, and deleting the profile dir is a fresh install
(the check §7's default-ON flip needs).

### Reading the game's files back out

IDBFS keeps one IndexedDB database named after the mount point, with an
object store `FILE_DATA` keyed by absolute path — so a driver can pull the
bytes out and hand them to the same checkers the native tests use:

```js
const db = await new Promise(res => {
  const r = indexedDB.open('/libsdl/cc.gfm/newtonia'); r.onsuccess = () => res(r.result); });
const store = db.transaction('FILE_DATA', 'readonly').objectStore('FILE_DATA');
const v = await new Promise(res => {
  const r = store.get('/libsdl/cc.gfm/newtonia/replays/current.nrp'); r.onsuccess = () => res(r.result); });
// v.contents is a Uint8Array -> write it to disk -> test/e2e/replay_check.py
```

**A file's presence proves nothing about its contents** — a 64-byte
`current.nrp` is a header with no records. Always run `replay_check.py` on
the bytes and read `last_slot` (slots are 10 Hz, so `last_slot/10` is
seconds of play persisted); that number is what distinguishes "saved the
run" from "saved the level boundary".

### The measurement that separates flushing from committing

Web writes are two-stage — a flush writes MEMFS, `FS.syncfs` commits to
IndexedDB on a later turn of the event loop — so "the data is missing"
has two very different causes. Force the hook by hand and vary only the
grace period before the close:

```sh
EXPLICIT=wait   node test/e2e/web_replay_tabkill.mjs   # 2.5 s before closing
EXPLICIT=nowait node test/e2e/web_replay_tabkill.mjs   # close immediately
```

Same code path, so a difference is entirely commit timing. Measured
2026-07-29: `wait` persisted 171 records (the whole 13.8 s run), `nowait`
26 (only up to the level boundary) — which is how we know the flush is
correct and no close-time hook can be made reliable. That result is why
the recorder flushes on a slot interval on web (`Recorder::record_delta`)
rather than trusting `pagehide`.

**Field-confirmed on Safari private browsing (2026-07-29)** once the
coalescer and the scaled interval shipped: no freeze through play, ESC to
menu or level clears, and no `FS.syncfs operations in flight` warning. The
container work sized the fix; only the browser that showed the bug can
close it.

### Replay end-states and crash artifacts (headless drivers)

Two conditions that only appear on files no CI test produces, both
reproducible with the Xvfb pattern from §4:

- **REPLAY ENDED (records ran out, no game over).** Record a run and
  ABANDON it (Esc to menu) so the file ends without a death, then play it
  back (`NEWTONIA_REPLAY_PLAY=current`) and outrun the recording. The card
  draws RETURN TO MENU; ENTER must return to the menu, not only ESC. Verify
  by screenshot — the process stays alive either way, so liveness proves
  nothing here.
- **Crash artifact header.** Play across a level boundary (a flush), then
  `kill -9` the process, and read the header with `replay_check.py`. A
  killed run never finalizes, so the check is whether the header still
  DESCRIBES the run: `score`/`generation`/`duration_ms` non-zero,
  `clean=0`. Comparing the same driver before and after a change is the
  useful form — the record count stays identical, so only the summary
  moves (2026-07-29: score 0/gen 0/dur 0 → score 31/gen 2/dur 4700 across
  52 records).

### Measuring replicated-ship rotation (the end-of-turn correction)

A replay ghost is a client with none of the setup: its records arrive at
the same 10 Hz cadence the wire uses, so any extrapolation artifact a
netplay client shows is reproducible from a file, single process, no
relay. The technique, used to size `NET_ROTATION_DAMP`:

1. Record a run of DELIBERATE turn/stop cycles — hold a rotate key ~1 s,
   then a full stop ~1 s, alternating. The artifact lives in the stop.
2. Play it back with a temporary trace printing the ghost's drawn facing
   (`Ship::facing` after `net_smooth_facing` — the reconcile rewinds
   `facing` and feeds the error back in, so `facing` IS what the player
   sees) and its `rotation_direction`, one line per 8 ms step.
3. Analyse per-step signed deltas around each rotating→stopped edge: how
   far the drawn facing continues in the turn's direction (lag being paid
   back) versus how far it travels BACKWARD (the visible correction).

Make the tuning constant env-overridable for the sweep and the whole
curve comes out of one recording. Measured 2026-07-29 over six cycles:

```
damp  reversal med  reversal max  fwd med  turn rate
 0.4         0.0°          1.3°    14.2°   2.25 deg/step
 0.6         0.0°          8.5°     7.0°   2.30
 0.8         4.2°         15.6°     0.0°   2.36
 1.0        11.4°         22.7°     0.0°   2.42
```

The turn-rate column is the check that matters as much as the reversal:
it barely moves, because the per-snapshot reconcile makes up whatever the
damping holds back. Damping changes WHERE the correction points, not how
fast the ship turns.
