# Join-link regression coverage and manual checklist

## Testing closed

Testing is closed at Glenn's request. Scenario 3c's revised expectation accepts
retaining the last full-capacity report during silent host network loss while
client seats remain reserved. Network loss does not immediately imply worker
grace; once the worker observes socket closure, grace admission applies.
Recovery and full-room refusal after recovery passed. No fixed production
silent-loss detection deadline was established by these tests.

Scenario 11 is closed with a coverage limitation: automated retry tests passed;
the device attempt was inconclusive, not a confirmed retry defect. There are no
reliable reproduction steps for an Android retry failure. No more manual tests
are scheduled; reopen only with fresh diagnostic evidence or a reproducible failure.


Updated 2026-09-15. Scenario numbers refer to `TESTPLAN-2026-09-15.md`.
Tested game commit: `46abba5a0d82befea8714d7c5507d3cdafe07313`, including
the full-room fix `23fd0480`. This pass adds tests and CI coverage; it does
not change game behaviour or deploy a worker/app.

## Automated results

| Scenario | Coverage | Result |
|---|---|---|
| 1 — dead code | Real desktop cold launch, fast failure, no retry loop | PASS |
| 2 — host left | Real host leaves its room, cold client receives terminal failure | PASS |
| 3 — full room | Four real players auto-start; fifth cold client and explicitly typed code both fail; zero-seat report checked | PASS |
| 3b — rejoin | Middle client paused until loss detection, then resumed in place; second bootstrap and `rejoin=1` checked; fresh join admitted while seat parked | PASS (process-pause fault) |
| 3c — relay loss | Per-host TCP proxy drops relay and holds reconnects for 20 s; grace admission, reclaim, renewed zero-seat report and refusal checked | PASS (real local relay outage) |
| 4 — silent relay | WebSocket upgrades but sends nothing; failure at 12.4 s | PASS |
| 4 extension — no offer | Relay says joined but never offers; cold invite fails at 45.5 s, no rejoin retry | PASS |
| 5 — spent clipboard | Clipboard contents verified; repeated automatic reads do not retry; typing that same code does retry | PASS |
| 6 — healthy invite | Cold launch reaches waiting room, then four-player bootstrap | PASS |
| 7 — patient recovery | Host process paused 20 s; clients enter patient rejoin and all bootstrap again | PASS (process-pause fault) |
| 8 — Steam friends | Platform UI and account handoff | MANUAL |
| 9–10 — install/one-shot | Production Java callback with fake Play service; durable commit before handoff, no second delivery, restart model | PASS logic; MANUAL Play integration |
| 11 — retry | Read exceptions, transient responses, delayed callbacks, failed preference commit, eventual single delivery | PASS logic; MANUAL Play integration |
| 12 — stale click | Callback drops a 25-hour-old click; exact 24-hour boundary and timestamp fallback tested | PASS logic; MANUAL Play metadata/clock integration |
| 13 — organic install | Empty and null referrers do not deliver invites | PASS logic; MANUAL store install |
| 14 — installed App Link | Java intent handoff/SDL guard plus native healthy/dead invite paths | PASS components; MANUAL Android URL dispatch |
| 15 — campaign install | Actual homepage campaign referrer has only UTM fields; complete campaign string produces no Java invite | PASS components; MANUAL store install |
| 16 — join routing | Desktop/iPhone/Android user agents, exact Steam URL, Play referrer, App Store button, banner metadata | PASS local and live; MANUAL OS launch/banner |
| 17 — missing code | `/join` renders the missing-code explanation on all three user agents | PASS local and live |
| 18 — leaderboard links | All three store links and Android CTA with score=1 | PASS local and live |
| 19 — promo suite | Expanded from 39 to 53 checks, including game banner regressions | PASS local and live site modes |

The live browser run checks deployed pages with a deterministic leaderboard
snapshot injected into the browser. Its game banner remains a local DOM fixture.
It does not validate live leaderboard data, launch Steam, install an app, or
render Safari's native Smart App Banner. Those are different integration layers.

Screenshots were inspected for the full-room failure card, typed-code status,
healthy waiting room, and patient rejoin card (`GIVING UP IN 59`). Native tests
assert state transitions from logs; screenshots are supporting visual evidence.
Recovery via process pause exercises silent-peer/watchdog behaviour; it does not
prove Wi-Fi, carrier, NAT, TURN, or mobile background behaviour.

Additional gates passed: worker room unit tests, capacity protocol tests,
host-close broadcast, reclaim chain, netplay-enabled build, and native WebRTC
self-test. Build: G++ 15.2.0, SDL2 2.32.10, SDL2_mixer 2.8.1, patched
libdatachannel 0.24.5. No compiler/linker warnings. Wrangler 4.131.2,
Node 24.19.0, Java 17, Chromium 153.0.8010.12 were used locally.

## Running the tests

Native driver dependencies: netplay build, Python 3, Node 22+, Xvfb,
xdotool, xclip, and either xwd/ImageMagick or Python Pillow with XCB support.
The shell entry point always creates its own display. Profiles and captures go
to `NEWTONIA_TEST_OUT` (use a fresh directory) or a new temporary directory.

```sh
# Terminal 1: dedicated LOCAL relay, with test-suite rate-limit headroom.
cd signal
npx wrangler@4 dev --local --ip 127.0.0.1 --port 8798 \
  --persist-to "$(mktemp -d)" \
  --var RATE_HOST_LIMIT:500 --var RATE_JOIN_LIMIT:1000

# Terminal 2, repository root:
make -j4
NEWTONIA_SIGNAL_URL=ws://127.0.0.1:8798/ws \
  test/e2e/join_link_scenarios.sh all
# Individual groups: invites or recovery. Allow up to 600 seconds for all.
python3 test/unit/android_install_referrer.py
node signal/test/room_unit_test.mjs
SIGNAL_TEST_URL=ws://127.0.0.1:8798/ws node signal/test/capacity_test.mjs
SIGNAL_TEST_URL=ws://127.0.0.1:8798/ws node signal/test/host_close_broadcast_test.mjs
SIGNAL_TEST_URL=ws://127.0.0.1:8798/ws node signal/test/reclaim_chain_test.mjs

npm install --no-save playwright typescript
npx playwright install chromium
PATH="$PWD/node_modules/.bin:$PATH" node test/e2e/web_promo_banner.mjs
PATH="$PWD/node_modules/.bin:$PATH" SITE_URL=https://newtonia.metonymous.com \
  node test/e2e/web_promo_banner.mjs
```

CI runs the native driver in the `join-links` shard and the local browser suite
in `Join links and promo routing`. The existing Android CI job runs the Java
referrer harness. Live-site checks are deliberately opt-in so deployment changes
do not determine whether an unrelated source checkout passes CI.

## Remaining manual checklist

Record the app version/build, worker target, device/OS, room code, approximate
timing, and result for each row. Verify the tested app and worker include the
full-room fix before judging scenario 3. The earlier handover said Android's
1.62.0 internal release was still a draft; rollout status was not changed or
verified in this local pass. The Play referrer cases require an actual Play
install from a tester-accessible release, not a sideloaded APK.

### Device/network confirmation

- [x] **3b — joiner loss and automatic rejoin: PASS (2026-09-15).**
  Glenn confirmed that the client rejoined successfully and the game continued
  normally while that client was absent. Build/worker identifiers and exact
  outage duration were not recorded.
- [x] **3b — fresh fifth client during the parked-seat window: PASS
  (2026-09-16).** Glenn confirmed that the fifth player joined successfully
  while the fourth player was disconnected, after only a few seconds.
  This confirms admission with a parked seat; the original player's return
  after the replacement joined was not part of this observation.
- [x] **3c — full-room refusal after recovery: PASS (2026-09-15).**
  After all four players rejoined, Glenn confirmed that a fifth client using
  the room link was refused as expected.
- [x] **3c — host network loss: ACCEPTED under revised expectation.**
  A separately connected fifth client received `THAT ROOM IS FULL` at 30
  and 60 seconds. Retaining the last full-capacity report during silent host
  loss is acceptable while player seats remain reserved. Network loss need
  not immediately enter worker grace. The local traffic-blackhole test
  reproduces this response; closing the socket admits a fresh join in grace.
- [x] **3c/7 — recovery after host outage: PASS for eventual recovery
  (2026-09-15).** Glenn reported that it took a while after restoration, but
  all clients eventually rejoined, approximately 30 seconds after the host
  network was restored.
- [x] **7 — waiting display and recovery: PASS (2026-09-16).** Glenn
  confirmed the waiting-for-host message and countdown appeared, followed by
  successful reconnection when the host's network was restored.

### Steam

- [x] **8 — macOS Steam friend join: PASS (2026-09-16).** Glenn confirmed
  that player 2 joined from macOS Steam through the Join Friend overlay using
  the local Steam build and the custom Launch Options command. Newtonia was
  closed before selecting Join Game, so cold launch is confirmed too.
- [x] **8 — dead-room Steam invite: PASS (2026-09-16).** Glenn resolved
  the invite UI by hosting on the Mac. Closing the host game removed the
  invite; after the follow-up test leaving the room, the joining client
  reported that the host had ended the game. Exact response time was not
  recorded. The earlier UI block is resolved.
- [x] **16 — website-to-Steam dispatch: PASS (2026-09-16).** Glenn
  confirmed that opening the room link in the browser and selecting Join on
  Steam launched the local Steam build and joined the waiting room with
  Newtonia previously closed.

### Android Play installs and App Links

- [x] **9 — Play install to automatic join: PASS (2026-09-16).** Glenn
  confirmed that installing through the room link's Google Play button and
  launching Newtonia automatically joined the room without entering a code.
- [x] **10 — install referrer is one-shot: PASS (2026-09-16).** Glenn
  confirmed that after leaving the game, force-stopping Newtonia and
  relaunching from its app icon, the normal menu appeared without a second
  automatic join attempt.
- [x] **11 — CLOSED: automated PASS; device attempt INCONCLUSIVE.** Install through a room link; make Play
  referrer access unavailable at first launch, then restore it and relaunch.
  The invite is delivered on the later successful read, once. A cached
  successful first read is inconclusive for this scenario, not a failure.
  Device attempt: first launch stayed at the menu after Play Store was
  force-stopped; reopening Play Store and force-stopping/relaunching Newtonia
  also did not join. No fresh referrer logs were observed; the stale-invite
  line supplied was historical. Result is inconclusive: a transient first-read
  failure was not established. No further device testing is scheduled. Any future attempt needs fresh
  diagnostics or controlled failure injection.
- [x] **12 — stale Play click: PASS for visible behaviour (2026-09-16).**
  Glenn confirmed that installing through a fresh room link, advancing the
  clock by 25 hours before first launch, then opening Newtonia produced the
  normal menu without a join attempt. The stale-referrer logcat diagnostic
  was not reported. Restore automatic date/time after the test.
- [x] **13 — organic install: PASS (2026-09-16).** Glenn confirmed that
  installing directly from Google Play without a room link launched to the
  normal menu, with no automatic join attempt.
- [x] **14 — installed Android App Link, live room: PASS (2026-09-16).**
  Glenn confirmed that tapping the live room link opened Newtonia and joined
  successfully. The exact Android build identifier was not recorded.
- [x] **14 — installed Android App Link, dead room: PASS (2026-09-16).**
  After the host left the room, Glenn tapped the saved link and the Android
  client reported that the host had ended the game. Exact response time
  was not recorded.
- [x] **15 — homepage campaign: PASS (2026-09-16).** Glenn confirmed
  that installing via the homepage Google Play card launched to the normal
  menu without an automatic room join.

### iPhone Safari

- [x] **16 — Safari App Store routing: PASS (2026-09-16).** Glenn
  confirmed that the App Store overlay displayed and installation succeeded.

Scenarios 1, 2, 3, 4, 5, 6, 17, 18 and the local/deployed-page portion of 19
have no remaining local logic checks. Platform-specific paste/controller UX can
be included in the device pass; the automated ordinary-join test uses explicit
typing because that is the desktop keyboard path.

## Automated reproduction of silent host loss

`signal/test/host_blackhole_test.mjs` reproduces the field refusal against
local Wrangler with the real worker. A TCP proxy drops host traffic in both
directions without closing either socket. After a host reports zero free
seats, fresh joins receive `room-full` at both 30 and 60 seconds. Closing the
proxy sockets then lets a fresh join enter host grace successfully.

This isolates the stale OPEN connection/capacity mechanism locally; it does
not establish Cloudflare's production TCP timeout or reproduce physical Wi-Fi
loss. The observed response is accepted under the revised expectation.
The script checks that response and the transition into grace after socket
closure. It is not wired into CI; no worker behaviour was changed.

Run a dedicated local worker, then:

```sh
SIGNAL_TEST_URL=ws://127.0.0.1:8798/ws node signal/test/host_blackhole_test.mjs
```

Android install-referrer retry and worker room unit tests were also rerun and
passed. Scenario 11's device result remains inconclusive; manual testing is
stopped at Glenn's request.
