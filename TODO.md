# TODO

Follow-ups that need a person, a device or an account we don't have to hand.

## Android cloud saves for old Play Games profiles: server saves (deferred)

**Problem.** A Play Games profile for Newtonia made before Saved Games was
switched on (2026-09-28) signs in and earns achievements, but every saved-game
open fails ("Cannot use snapshots without enabling the 'Saved Game' feature",
then SIGN_IN_REQUIRED). `PlayGamesSaves` recognises it, logs "this Play Games
profile has no Saved Games access" and leaves sync off; the game plays on its
local files. Only deleting the game's Play Games profile on that Google account
fixes it, and we won't ask players to do that.

**Ruled out (2026-10-05, on a real old-profile account, PR #585 closed):**
- `requestServerSideAccess` consent calls: NPE inside the service with
  `[OPEN_ID]`, "Not signed in when calling API" without scopes.
- A `drive.appdata` grant through Google's AuthorizationClient: granted,
  opens still fail, also after a full restart.
- Clearing Google Play services data: a fresh sign-in, scope already granted,
  open still fails. So it is not anything cached on the phone; it is
  Google's server-side record for the profile. No client call changes it.
- Re-publishing the Play Console config, Drive API or OAuth screen edits:
  new profiles already sync on the same build, so the config is right.

Decision: leave old profiles unsynced for now (Glenn, 2026-10-05). If enough
players hit it, build server saves as below.

**Strategy: store the bundle on the board worker.**
1. *Identity.* The board worker already verifies Play Games players:
   `PlayGamesIdentity.fetchCode()` mints a `requestServerSideAccess` code
   (no extra scopes; works on old profiles today, the leaderboard uses it),
   and `verify_identity` in `board/src/worker.js` redeems it to the account
   `pg:<playerId>`. Reuse that; no new credential.
2. *Storage.* New route on the board worker: GET / PUT of the existing
   `CloudMerge` bundle (savegame + highscore + stats, a few KB) in the
   `REPLAYS` R2 bucket under `saves/pg/<sha256(account)>`, with an ETag
   compare-and-swap on PUT. The worker does no merging: on a 412 the client
   re-reads, folds with `nativeMergeBundles`, and puts again (the same
   read-merge-write the Snapshots path does). Per-account rate limit and a
   size cap, like the replay submit path.
3. *Client.* A second backend `Store` behind `cloud_sync_core.h`, used when
   `PlayGamesSaves.parked()` fires (profile has no Saved Games access). Same
   merge rules (high score max, stats per-counter max, newest save wins), so
   nothing else in the game changes. Option to later make it the only Android
   path, importing the Snapshots copy once so post-09-28 saves aren't lost.
4. *Testing.* Unit tests for the route in the board suite plus the
   `wrangler dev --local` protocol test; field test with a debug APK pointed
   at the beta worker (`NEWTONIA_BOARD_URL=wss://newtonia-board-beta.gfmcc.workers.dev/board`)
   on the old-profile test account (Glenn's second phone; keep that profile),
   checking `adb logcat -s NewtoniaCloudSync` and a round trip across a
   reinstall.
5. *Privacy.* The account id already travels client to worker over wss for
   leaderboard attestation; the R2 key is hashed. Say in the privacy text
   that Android progress may be stored on our server.

Worth doing alongside, at no code cost: report the old-profile behaviour to
Google (issuetracker.google.com, Play Games Services) with the logs above.

## Newtonia 2: a new game on SDL3 (planned)

**Decision (Glenn, 2026-10-03).** Newtonia feels finished, and the
motivation is a fresh perspective, so the next project is a new game, not a
2.0 update. Newtonia 1 goes to maintenance: bug fixes and store upkeep only.
It stays a paid game. Tech: SDL3, chosen over Godot (also weighed: Unity,
Rust + Bevy).

**Why a separate game.** A paid release on Steam gets its own launch
visibility and price, which an update never does.

**Tech plan (fresh how, not just a fresh library):**
- SDL3's main callbacks (`SDL_AppInit`/`SDL_AppIterate`): one entry point
  for desktop, web, iOS and Android, no per-platform main files like
  `glut.cpp` or `android_main.cpp`.
- SDL_GPU for rendering (Vulkan, Metal, D3D12) instead of the GL/GLES compat
  layer. Check first: SDL_GPU may have no web backend yet, in which case
  web needs a small GL fallback or comes later.
- C++20 instead of C++11.
- A deterministic simulation (fixed step, seeded RNG, no wall clock in game
  logic), separate from rendering from day one. That gives replays, seeded
  runs and leaderboard entries the server can check without Newtonia's
  snapshot machinery.
- Carry over: the Cloudflare signal and board workers, the CI and deploy
  workflow patterns, and the ideas behind the shot harness and headless
  e2e tests, rewritten small. Don't carry over the game code.

**Design approach:**
- Prototype two or three rough ideas, about a week each, played on the
  phone; keep the one still worth playing after a week. Only call it
  Newtonia 2 if it keeps the Newtonian drift.
- Candidate directions: gravity puzzle-action (place gravity wells rather
  than fly a ship); tethered co-op (two ships on a rope that swing and fling
  things); a run-based shooter (daily seeded runs, an upgrade pick after
  each level, portrait phone first).

**Launch plan:**
- Steam "Coming soon" page as soon as a prototype looks right, so wishlists
  build during development.
- A free demo in a Steam Next Fest.
- Steam (with the Deck) first, web as the free demo; iOS and Android after,
  since paid mobile games sell poorly.

**First step:** a new repo with an SDL3 app showing a ship you can fly, built
in CI for Linux, Windows, macOS and web, with a headless screenshot test.
Open: the repo name, and private or public.
