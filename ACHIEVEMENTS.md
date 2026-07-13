# Achievements — shared design & platform requirements

Platform-neutral spec for Newtonia's achievements system. Everything in this
document is shared game-side work: the seam, the game-logic hooks, lifetime
stats, and the unlock-integrity rules. Platform backends plug in behind it:

| Backend | Where it lives | Status |
|---------|----------------|--------|
| Xbox / Microsoft Store (GDK) | private GDKX mirror (`xbox/PRIVATE_REPO.md`) | cert-blocking |
| Steam | this repo, `steam_achievements.cpp` | **implemented** — portal config pending (§2 checklist) |
| Android (Google Play Games Services) | this repo, `play_games_achievements.cpp` + `PlayGamesAchievements.java` | **implemented** — console configured + IDs wired 2026-07-13; testers/publish pending (§2 checklist) |
| iOS (Game Center) | this repo, `game_center_achievements.mm` | **implemented** — achievements entered in App Store Connect 2026-07-13; go live with the next submitted version (§2 checklist) |

Why now: Xbox certification **requires** achievements. XR-055 scores "a game
doesn't support the minimum 10 achievements and 1,000 gamerscore" as
Critical (12) — an automatic cert failure — so the Xbox release cannot ship
without them. Xbox's rules are the strictest of the four platforms, so they
set the minimums; a list and seam that satisfy Xbox work everywhere else.

Sources (public GDK docs, retrieved 2026-07-11):
- XR-055 FMA severities: https://learn.microsoft.com/en-us/gaming/gdk/docs/store/policies/fma/xr055-achievements?view=gdk-2604
- XR-057 Unlocking Achievements: https://learn.microsoft.com/en-us/gaming/gdk/docs/store/policies/xr/xr057?view=gdk-2604
- Achievements Manager API overview: https://learn.microsoft.com/en-us/gaming/gdk/docs/services/player-data/achievements/achievements-manager/live-achievements-manager-overview?view=gdk-2604

## 1. Xbox certification requirements (these set the minimums)

Hard limits from XR-055 (apply to Xbox One, Xbox Series, **and Windows** —
i.e. both Play Anywhere SKUs; one achievement config per Title ID covers
both):

| Rule | Limit | Severity if violated |
|------|-------|----------------------|
| Minimum at launch | **10 achievements, 1,000 gamerscore** | Critical (12) |
| Base gamerscore cap | **exactly 1,000** configured as base achievements | Critical (12) |
| Single achievement | ≤ **200** gamerscore | Critical (12) |
| Post-launch additions | +100 achievements / +1,000 GS per half-year | — |

Design rules from the XR-055 severity table (Critical unless noted):

- All achievements must be genuinely earnable, and unlock exactly when their
  criteria are met, **to the profile/account that met them**.
- Not all obtainable within the first few minutes, nor after exploring
  **less than half** the game content.
- No "vast number" of achievements that don't represent real engagement, and
  no multiple achievements needing little/no input.
- Unlocks must survive suspend → resume (lost-but-recovered-on-relaunch is
  Minor; lost entirely is Critical).
- Earns made offline must post when connectivity returns — the player must
  never have to re-earn (Very Minor, handled by the platform API if used
  correctly).
- Descriptions should state any required mode/difficulty (Very Minor).
- Easy-mode completion must not unlock a hard-mode achievement (Moderate).

These are good design hygiene on every platform, not just where they're
audited — treat them as the house rules for the whole system.

### XR-057 — how unlocks may happen

Every base achievement must be earnable through gameplay with **no purchase
required** (trivially true for Newtonia — no in-title purchases). The rule
that bites us: **no unlock via cheat codes or menu options** that skip the
corresponding gameplay. Microsoft's own line: completing a level *while a
cheat is active* (e.g. invincibility) may still unlock — developer's choice —
but a cheat that **skips** content must not unlock the achievement for
*completing* it (a "start level 2" achievement may unlock via a level-skip
cheat; a "complete level 1" achievement must not).

**Newtonia-specific hazard:** the skip-level (`N`) and time-scale (`=`/`-`)
keys exist in shared code (keyboard-only, hidden from the controller help
overlay, but present). Policy, enforced in the shared layer so every backend
inherits it: **any cheat key used → suppress all unlocks for the rest of the
game** (a `cheated` flag set on skip or time-scale change, cleared only when
a new game starts, and persisted in the savegame so save/quit/resume doesn't
launder it). A per-generation reset was considered and rejected: it would let
a player skip to one generation short of a progression achievement and
legitimately clear a single level to unlock it.

## 2. The other platforms

None of the others impose Xbox-style minimums, so the same master list (§5)
ships everywhere; per-store extras can be added later without disturbing
Xbox's fixed 1,000-GS base budget. What differs is only backend plumbing:

- **Steam** — `SetAchievement` / `IndicateAchievementProgress` (stats-backed);
  achievements defined in the Steamworks portal under `ACH_*`-style API names.
- **Google Play Games Services** — unlock + incremental achievements
  (steps-based; the seam's percent maps to steps); IDs generated by the Play
  Console.
- **Apple Game Center** — `GKAchievement` with native `percentComplete`
  (the seam's percent maps directly); reverse-DNS identifiers configured in
  App Store Connect.

Common to all four: the platform shell draws the unlock toast/banner (no
in-game achievement UI needed), definitions live in each store's portal (so
the §5 list is the single master, frozen once), and offline earns are queued
by the platform SDK — where a backend's SDK doesn't queue reliably, the
backend owns retrying, not the game code.

### Steam backend (`steam_achievements.cpp`)

Implemented upstream behind `STEAM_BUILD` (the flag the deploy-steam
workflow already sets; `steam_init()`/`steam_run_callbacks()` were already
in the desktop loop). Per Valve's directions
(https://partner.steamgames.com/doc/features/achievements):

- **Unlocks** call `SetAchievement` + immediate `StoreStats` (the toast
  should be instant). An in-memory unlocked cache keeps the seam's
  idempotent re-unlocks from spamming the rate-limited `StoreStats`.
- **Progress** maps the seam's 0–100 percent onto per-achievement
  **increment-only INT stats** (`SetStat` is documented as a cheap
  in-memory write). The portal binds each stat to its achievement as the
  "Progress Stat" with unlock value 100, which gives Community progress
  bars, server-side auto-unlock at the threshold, and Steam's
  higher-value merge across devices. `StoreStats` for stat progress is
  throttled to ~60 s checkpoints per Valve's guidance; unlock stores
  flush anything pending.
- **Init timing:** targets SDK 1.61+ (stats auto-requested at
  `SteamAPI_Init`; `RequestCurrentStats` is gone). Anything set before
  `UserStatsReceived_t` fires is queued and flushed by the callback.
  `UserStatsStored_t` failures are logged — `k_EResultInvalidParam`
  means a name isn't published in the portal.
- **Offline** is Valve-handled: "Steam keeps a local cache of the stats
  and achievement data so that the APIs can be used as normal in offline
  mode" — no journal needed on this platform.

**Steamworks portal checklist (no code):**

1. Define the 18 achievements under App Admin → Stats & Achievements with
   exactly these API names (the mapping table in `steam_achievements.cpp`
   is authoritative): `ACH_FIRST_KILL`, `ACH_CLEAR_LEVEL1`,
   `ACH_SPECIALS_7`, `ACH_BLACK_HOLE_SURVIVOR`, `ACH_MINI_STATION_KILL`,
   `ACH_SHIELD_RAM`, `ACH_SHIELD_RAM_ASTEROID`, `ACH_STATION_DESTROYED`,
   `ACH_ENEMIES_10`, `ACH_NOVA_DETONATED`, `ACH_NO_DAMAGE_CLEAR`,
   `ACH_NO_SECONDARY_LEVEL10`, `ACH_WEAPONS_7`, `ACH_COOP_CLEAR`,
   `ACH_KILLS_1000`, `ACH_KILLS_10000_LIFETIME`, `ACH_SCORE_3M`,
   `ACH_REACH_LEVEL15`.
   Names/descriptions from the §5 table. Icons (achieved + greyscale
   locked, 256×256) are generated in the game's vector style by
   `generate_achievement_icons.py` into `steam/icons/` — regenerate after
   any list change. 18 is well inside the initial 100-achievement limit.
2. Define 7 INT stats — `specials_7_pct`, `weapons_7_pct`,
   `enemies_10_pct`, `kills_1000_pct`, `kills_10000_lifetime_pct`,
   `score_3m_pct`, `reach_level15_pct` — each min 0, max 100, default 0,
   **increment-only**, and bind each to its achievement as the Progress
   Stat with unlock value 100.
3. Add `stats.dat` (plus `highscore.dat`/`savegame.dat` if desired) to the
   depot's **Auto-Cloud** file patterns so lifetime stats roam (§4).
4. Publish the changes, then test on the `beta` branch build — unlocks
   show as overlay toasts. Reset a test account with
   `ISteamUserStats::ResetAllStats(true)` or the Steam console
   (`steam.exe -console`, then `reset_all_stats <appid>` /
   `achievement_clear <appid> <name>`).

### Game Center backend (`game_center_achievements.mm`)

Implemented upstream behind `GAME_CENTER_BUILD` (defined by the iOS Xcode
project, which also links GameKit.framework). `Achievements::init()` is
called from `ios_main.mm` at startup and sets the GameKit
`authenticateHandler` — that is what triggers sign-in; the prompt presents
over the SDL window once it exists.

- **Unlocks and progress are the same call.** `GKAchievement.percentComplete`
  is natively 0–100, so the seam's percent maps 1:1 and event-only
  achievements are a single report of 100. GameKit draws the unlock banner
  (`showsCompletionBanner`) and the server keeps the highest percent it has
  ever seen, so re-sends never regress anything.
- **Every earn is journaled before the delivery attempt** (§2 offline
  earns): GameKit does not reliably queue offline reports, so earns go into
  the shared pending journal, keyed by the authenticated player
  (`teamPlayerID`) so an account switch can never credit one player's earn
  to another (XR-055's correct-profile rule, §1). Entries are confirmed
  out only when the server's achievement list actually shows the earn — a
  nil `reportAchievements:` error alone is not trusted, because a batch
  can "succeed" while the server silently drops an identifier that isn't
  defined in App Store Connect (a typo'd portal entry would otherwise eat
  the earn); a missing-after-accept identifier is logged and stays
  journaled. Unconfirmed entries resubmit on sign-in and app foreground;
  sign-in first loads the server's achievement list and confirms away
  anything it already has — the reconcile pass, which also makes
  cross-device double-earns cheap no-ops.
- **Send cadence:** a Game Center report is a network RPC (unlike Steam's
  in-memory `SetStat`), so progress-only changes batch on a 60 s interval;
  a new unlock flushes the whole journal immediately so its banner is
  instant (unless it raced an in-flight send whose batch then failed — then
  it rides the next earn/foreground/interval; delivery is never lost). The
  journal batches its own disk writes the same way stats.dat does: unlocks
  write through, progress advances flush on the send cadence and app
  resign-active. Signed-out play earns nothing on Game Center until the
  device's account signs back in — unowned journal entries resolve to the
  device's last-signed-in account, never to a different account.
- **`coop_clear` is deliberately unmapped** — deferred until netplay makes
  it earnable on touch devices (§5). Unmapped earns drop silently in the
  backend; the shared hook keeps firing, so adding the mapping later is the
  whole change.
- **Dev reset:** launch once with `NEWTONIA_RESET_GAME_CENTER=1` (set via
  the Xcode scheme) to wipe the signed-in account's Game Center
  achievements and drop the local pending journal, then quit and relaunch
  without it. Delete `stats.dat` too — the lifetime counters would
  otherwise re-earn `specials_7`/`kills_10000_lifetime` progress
  immediately (same caveat as the Steam reset).

**App Store Connect checklist (no code):**

1. In the Developer Portal, enable the **Game Center capability** on the
   `cc.gfm.Newtonia` App ID and regenerate the distribution provisioning
   profile — `ios/Entitlements.plist` now carries
   `com.apple.developer.game-center`, and signing fails if the profile
   lacks it. **Then re-encode the new `.mobileprovision` into the
   `PROVISIONING_PROFILE_BASE64` GitHub secret** (`base64 -w0` /
   macOS `base64 -i`) — deploy-ios.yml signs with the secret, not the
   portal, so the deploy stays broken until the secret is updated. Then
   turn Game Center on for the app in App Store Connect. (Local Xcode
   device builds sign with `ios/EntitlementsDev.plist` via the project's
   `CODE_SIGN_ENTITLEMENTS`, so sandbox testing works without any of
   this — automatic signing manages the dev profile.) **Done 2026-07-13 —
   capability enabled, profile regenerated, and the secret re-encoded.**
2. Define **17 achievements** (all of §5 except `coop_clear`) with exactly
   the reverse-DNS identifiers from the mapping table in
   `game_center_achievements.mm` (the table is authoritative):
   `cc.gfm.newtonia.first_kill` … `cc.gfm.newtonia.reach_level15`.
   All visible (none hidden), standard one-time achievements — Game Center
   needs no incremental configuration; progress is just the reported
   percent. **Done — entered 2026-07-13; the portal-text table below
   records exactly what was entered.**
3. Points: Game Center caps a single achievement at 100 points (1,000
   total), so use the §5 GS values with `station_destroyed` capped at
   **100** — total 900, leaving 100 headroom for `coop_clear` (40) at
   netplay time plus future additions. **Done — see the table below.**
4. Icons: upload `gamecenter/icons/<id>.png` (512×512, generated by
   `generate_achievement_icons.py`; no locked variant — Game Center
   renders its own locked state).
5. Achievements go live with a **version**: definitions are app-level (and
   work in sandbox immediately), but public availability requires turning
   the Game Center section on in the next submitted App Store version and
   attaching the achievements to it — already-released versions are
   immutable, so this rides the release that ships the entitled binary.
6. Test with a development/TestFlight build (sandbox Game Center is
   automatic there): earns show as GameKit banners; use the dev reset
   above to re-test from scratch.

**Portal text as entered in App Store Connect (2026-07-13).** Achievement
definitions live only in Apple's portal, so this table is the repo's copy
of record — keep it in sync with any portal edits. Wording deviations from
§5 are deliberate: Myriad spells out "across all your games" to
distinguish the lifetime count from Millennium's one-game count (the
state-your-requirements rule), and Untouchable's earned text drops the
"level 9 or beyond" qualifier since the requirement only matters before
the earn.

| Achievement ID | Display Name | Pre-Earned Description | Earned Description | Points |
|---|---|---|---|---|
| `cc.gfm.newtonia.first_kill` | First Blood | Destroy your first asteroid | You destroyed your first asteroid | 10 |
| `cc.gfm.newtonia.clear_level1` | Clear Skies | Clear level 1 | You cleared level 1 | 20 |
| `cc.gfm.newtonia.specials_7` | Special Delivery | Destroy 7 different asteroid types | You destroyed 7 different asteroid types | 100 |
| `cc.gfm.newtonia.black_hole_survivor` | Event Horizon | Survive a black-hole level (level 10 onward) without dying | You survived a black-hole level without dying | 80 |
| `cc.gfm.newtonia.mini_station_kill` | Little Nuisance | Destroy a mini-station | You destroyed a mini-station | 50 |
| `cc.gfm.newtonia.shield_ram` | Battering Ram | Destroy an enemy by ramming it with your shield active | You destroyed an enemy by ramming it with your shield active | 20 |
| `cc.gfm.newtonia.shield_ram_asteroid` | Icebreaker | Ram an asteroid with your shield active | You rammed an asteroid with your shield active | 10 |
| `cc.gfm.newtonia.station_destroyed` | Station to Station | Destroy the enemy station (appears at level 15) | You destroyed the enemy station | 100 |
| `cc.gfm.newtonia.enemies_10` | Ace | Destroy 10 enemy ships in one game | You destroyed 10 enemy ships in one game | 40 |
| `cc.gfm.newtonia.nova_detonated` | Nova | Detonate a nova | You detonated a nova | 60 |
| `cc.gfm.newtonia.no_damage_clear` | Untouchable | Clear level 9 or beyond without taking damage | You cleared a level without taking damage | 100 |
| `cc.gfm.newtonia.no_secondary_level10` | Purist | Reach level 10 without using a secondary weapon | You reached level 10 without using a secondary weapon | 60 |
| `cc.gfm.newtonia.weapons_7` | Full Arsenal | Fire 7 different weapon types in one game | You fired 7 different weapon types in one game | 60 |
| `cc.gfm.newtonia.kills_1000` | Millennium | Destroy 1,000 asteroids in one game | You destroyed 1,000 asteroids in one game | 50 |
| `cc.gfm.newtonia.kills_10000_lifetime` | Myriad | Destroy 10,000 asteroids across all your games | You destroyed 10,000 asteroids across all your games | 40 |
| `cc.gfm.newtonia.score_3m` | Megascore | Score 3,000,000 points in one game | You scored 3,000,000 points in one game | 60 |
| `cc.gfm.newtonia.reach_level15` | Deep Space | Reach level 15 | You reached level 15 | 40 |
| | | | **Total** | **900** |

### Play Games backend (`play_games_achievements.cpp` + `PlayGamesAchievements.java`)

Implemented upstream behind `PLAY_GAMES_BUILD`, which the root
`CMakeLists.txt` defines for every Android build (that CMake file builds
nothing else). The Play Games v2 SDK is Java-only, so the backend is split:
the native half owns the authoritative symbolic→resource-name mapping table
and forwards earns over JNI (from the game thread, deduped through a
best-sent cache so the seam's idempotent re-fires don't spam JNI); the Java
half (`android/app/src/main/java/org/newtonia/PlayGamesAchievements.java`)
marshals everything to the UI thread and owns the SDK calls.
`Achievements::init()` is called from `android_main.cpp` after `SDL_Init`
(the JNI activity handle must exist) and initialises `PlayGamesSdk`, which
kicks off the v2 SDK's **automatic sign-in** — there is no sign-in UI in
the game.

- **Unlocks** call `AchievementsClient.unlock()`; the Play Games shell
  draws the toast. **Progress** maps the seam's percent onto **incremental
  achievements defined with 100 steps** via `setSteps(id, pct)` — the same
  seven counter-backed achievements that get progress stats on Steam.
  `setSteps` is "at least this many steps", so lower values are server-side
  no-ops (monotonic by construction) and reaching 100 steps unlocks the
  achievement server-side — the backend never calls `unlock()` on an
  incremental definition, which the Play API disallows.
- **IDs live in `res/values/games-ids.xml`**: the Play Console generates
  opaque IDs, pasted there under the resource names fixed by the native
  mapping table (`achievement_<symbolic>` — keep these names; the console's
  own "Get resources" export derives different names from the display
  names). A missing resource drops that achievement's earns with a one-shot
  logcat warning, never a crash — so the backend ships ahead of the portal
  config and each console entry landing later is the whole fix.
- **Offline is Google-handled** (§2 "Offline earns"): once signed in,
  `unlock`/`setSteps` are cached on-device by Play services and synced when
  connectivity returns — no pending journal on this platform. The only gap
  is the pre-sign-in window: earns arriving before the async sign-in check
  resolves (or while signed out) are held in an in-memory queue merged per
  achievement at max percent, flushed when a later check succeeds —
  re-checked on every activity resume (`NewtoniaActivity.onResume`).
  Signed-out play earning nothing if the process dies first is allowed
  (XR-055), and the counter-backed achievements re-derive from persisted
  stats next session anyway.
- **`coop_clear` is deliberately unmapped** — same decision as Game Center
  (§5): deferred until netplay makes it earnable on touch devices.
- **Failure-proof by design:** a missing `APP_ID` meta-data or absent Play
  services logs a warning and disables achievements for the session — the
  game must never crash over Games config, since the placeholder ships
  before the Play Console project exists.

**Play Console checklist (no code):** immediately actionable — the app is
already live on the Play internal testing track (deploy-android.yml), so
achievements can be configured and then tested with the existing internal
testers. Until then, builds carrying the placeholder project ID are safe to
ship: sign-in fails silently and earns are held/dropped per the backend
rules above.

1. In the Play Console, set up **Play Games Services** for the app
   (Grow → Play Games Services → Setup and management → Configuration),
   create the game project, and link the `org.newtonia` app (adds the OAuth
   client / SHA-1 for the signing key — credentials are needed for **both**
   the upload key and the Play App Signing key, or sign-in works in local
   builds but fails on Play-delivered internal-testing builds, and vice
   versa). **Done 2026-07-13** — game project 717199808901, two Android
   credentials (Play-signed + upload key, anti-piracy off, Play-signed
   marked "use for new installs"). SHA-1s live on the OAuth clients in
   Cloud Console (APIs & Services → Credentials), not in the Play
   credential UI. A third OAuth client/credential for a dev machine's
   debug keystore can be added the same way for `assembleDebug` testing.
2. Replace the placeholder `game_services_project_id` in
   `android/app/src/main/res/values/games-ids.xml` with the numeric project
   ID the console assigns. **Done 2026-07-13.**
3. Define **17 achievements** (all of §5 except `coop_clear`) with the §5
   names/descriptions. Define these seven as **incremental with 100 steps**:
   `specials_7`, `enemies_10`, `weapons_7`, `kills_1000`,
   `kills_10000_lifetime`, `score_3m`, `reach_level15`; the rest are
   standard. Play awards XP rather than gamerscore — scale the §5 GS values
   at definition time (XP must be a multiple of 5, 5–200 per achievement;
   the ratios are what matter). **Done 2026-07-13** via the console's ZIP
   bulk import (CSVs + icons; the CSV format forbids commas in
   names/descriptions, so Millennium/Myriad/Megascore use "1000" /
   "10000" / "3 million"; the Game Center point values were used, sum
   900). With only the default locale, AchievementsLocalizations.csv must
   be present but EMPTY — en-US rows are rejected as "wrong locale".
4. Paste each generated achievement ID into `games-ids.xml` under the
   resource name from the mapping table in `play_games_achievements.cpp`
   (keep the names exactly). **Done 2026-07-13** — all 17 wired and
   cross-checked against the mapping table.
5. Icons: upload 512×512 icons — reuse `gamecenter/icons/<id>.png`
   (generated by `generate_achievement_icons.py`; Play renders its own
   locked state, so the achieved art is all it needs). **Done 2026-07-13**
   (rode the ZIP import).
6. Add tester accounts under Play Games Services → Testers (separate from
   the app's internal-testing testers) — while the games config is a
   draft, only listed testers can sign in — and test with a
   correctly-signed build from the internal track (or a local
   release-signed build): earns show as Play Games toasts, sign-in state
   is greppable via `adb logcat -s NewtoniaPlayGames`. Then **publish**
   the Games Services configuration to open sign-in to everyone —
   deliberately after tester validation, since published achievements are
   permanent (they can be edited but never deleted). There is no
   client-side reset API — reset a test account's earns via the console
   or the Play Games Management API.

### Offline earns

The game never checks connectivity: hooks call `unlock()`/`progress()` the
moment criteria are met, and **delivery is each backend's responsibility**
(XR-055: an offline earn must post when connectivity returns — the player
must never re-earn it; lost-but-recovered-on-relaunch is Minor, lost
entirely is Critical).

Per platform:

- **Steam** — the Steam client stores `SetAchievement` calls locally and
  syncs when it reconnects. Nothing for the backend to do.
- **Google Play Games** — the SDK caches unlock/increment calls offline and
  flushes them on reconnect. Nothing to do beyond the backend's in-memory
  pre-sign-in queue (see the Play Games backend section).
- **Xbox / GDK** — unlock calls need Xbox Live, and the GDK does **not**
  queue them offline: the backend must persist a small pending-unlocks
  journal (same pref-path pattern as `stats.dat`), retry on launch / resume
  / connectivity-regained, and drop an entry only on confirmed post.
  (Signed-out play earning nothing is allowed; offline-while-signed-in must
  recover.)
- **Game Center** — `reportAchievements` can fail offline and GameKit does
  not reliably queue, so the iOS backend uses the persist-and-resubmit
  journal (implemented — see the Game Center backend section above).

Two properties of the shared layer make retries trivially safe:

1. **Unlocks are idempotent and progress is monotonic** — a backend can
   blindly re-send anything it is unsure about; double-posting is harmless
   on every platform.
2. **The facts behind most achievements are persisted locally** (lifetime
   kills + specials mask in `stats.dat`; per-game counters and the cheat
   flag in the savegame), so a backend can run a **reconcile pass** at
   startup or sign-in: re-derive progress from the persisted counters and
   re-report it. Even a lost journal entry is then re-earned from ground
   truth without the player redoing anything.

The exception: event-only achievements with no counter behind them
(`nova_detonated`, `black_hole_survivor`, `coop_clear`, `clear_level1`,
`no_damage_clear`, `no_secondary_level10`, `mini_station_kill`,
`shield_ram`, `shield_ram_asteroid`, `station_destroyed`) cannot be
re-derived, so the pending journal is their only safety net. The journal
exists as the planned shared utility next to the seam —
`achievement_journal.h/.cpp`, keyed by symbolic ID **plus the platform
account that earned it** (earns made before sign-in resolve to the
device's last-signed-in account, so a profile switch can never
cross-credit — XR-055's correct-profile rule), earns merged per-key at
their maximum percent, persisted as `pending_achievements.dat` in the SDL
pref path — built alongside the Game Center backend (which landed first);
the GDK backend consumes it as-is (single-threaded: marshal SDK
completions to the game thread, as the Game Center backend does).

## 3. The seam (`achievements.h`)

Mirror the `SaveStorage` pattern (`save_storage.h/.cpp`): a tiny
platform-neutral interface, with each platform's backend behind it and a
no-op default so every other build compiles unchanged.

```cpp
namespace Achievements {
    void unlock(const char* id);            // e.g. unlock("station_destroyed")
    void progress(const char* id, int pct); // 1-100; 100 == unlock
}
```

- **Symbolic IDs in code, per-backend mapping in the backend.** Call sites
  use stable symbolic names (`"station_destroyed"`), never platform IDs.
  Each backend owns a small symbolic→platform-ID table (Partner Center:
  numeric strings; Steamworks: `ACH_*` names; Play Console: generated IDs;
  Game Center: reverse-DNS). Renaming in any portal never touches game code.
- **Lowest-common-denominator semantics.** `unlock()` + integer-percent
  `progress()` map onto all four backends (percent → Xbox percent, Steam
  progress indication, Play steps, Game Center `percentComplete`). No
  score/points or UI concepts in the seam.
- **The XR-057 cheat-suppression flag lives in the shared layer**, so every
  backend inherits it for free.
- **Default backend is an inline no-op** — builds without a backend (e.g.
  web, plain desktop) carry zero cost and zero new dependencies.

### Hook points

One-line calls where the events already happen — no platform code leaks into
logic classes (repo convention #1):

- `GLGame`: kill credited to a player (per-bullet owner already tracked),
  generation cleared, generation reached, mini-station destroyed
  (`GLMiniStation` reward path), station destroyed, black-hole generation
  survived, pickup/weapon usage.
- `Ship`: `nova_detonate()`, damage taken (for no-damage tracking).

## 4. Lifetime stats — standalone roaming `stats.dat`

The lifetime-kills counter lives in its own small **`stats.dat`** under the
`SaveStorage` **Roaming** category (same as `highscore.dat`) — deliberately
**not** inside `savegame.dat`:

- **Netplay-proof.** The planned P2P netplay (`claude/netplay-milestone1`)
  won't load or save the campaign savegame — a counter inside it would stop
  counting in exactly the mode with the most kills. A standalone stats file
  is written on kill/interval/game-end regardless of game mode.
- **Roams with the person.** On platforms with roaming storage the counter
  follows the account across devices — matching how the achievements
  themselves roam. No savegame VERSION bump needed.
- **Steam Cloud:** `stats.dat` likely needs adding to the depot's Steam
  Auto-Cloud file patterns so lifetime stats persist across installs and
  machines — the achievements roam via Steam, so their backing stats must
  too. Decide alongside the Steamworks backend (`highscore.dat` and
  `savegame.dat` deserve the same call).

Implementation status: `SaveStorage` does not exist upstream yet (it is a
planned abstraction — `xbox/PORT_PLAN.md` Phase 4 / work item 10), so
`stats.h/.cpp` mirrors `highscore.dat`'s SDL-pref-path + IDBFS pattern
directly; when SaveStorage lands, the file I/O moves behind it unchanged.

**Attribution rule (couch 2P and netplay alike):** kills are already
credited per player (bullets carry an owner for scoring). The hook is "kill
credited to player N → increment lifetime stats **only if player N is
local**" — remote peers count their own kills on their own machine. On Xbox
this also satisfies XR-055's correct-profile-only rule by construction.
Residual netplay risk — a modified peer feeding fake kill events — is
self-cheat exposure only, the same as a local memory editor.

**Cheat games don't accrue lifetime stats either.** The game-scoped cheat
flag (§1) suppresses more than `unlock`/`progress`: kill writes to
`stats.dat` are also skipped while it is set. `specials_7` and
`kills_10000_lifetime` read the lifetime file, so a dev-start run would
otherwise bank progress there for a later clean game to cash in — the exact
laundering the flag exists to prevent. (Found live: a heavily-playtested
machine unlocked `specials_7` on the first kill of the first clean Steam
game, from a mask partly filled during dev-start sessions.)

## 5. Candidate achievement list (draft — must sum to exactly 1,000 GS)

Master list for **every store**. Tune names/values before freezing IDs in
any portal (they get baked into the backend mapping tables). The GS column
is Xbox-only; other platforms ignore it (or map it to their own point
schemes at backend-definition time).

**Terminology rule:** user-facing names and criteria always use the
**displayed level number** (the HUD's `LEVEL = internal generation + 1`) —
players never see "generation". Code hooks translate: the black hole appears
at internal generation 13 = level 14; the station at internal generation 14 =
level 15.

**Future-proofing rule:** design criteria to survive content additions —
never "all X". Count-based targets frozen at today's numbers instead
(`specials_7`, `weapons_7`): adding an 8th special type or weapon widens the
pool a player can pick 7 from, it never raises the bar or retroactively
devalues an unlock. New content earns a *new* achievement (post-launch GS
budget) rather than mutating an old one. Entity-anchored triggers
(`station_destroyed`) follow the same idea — the trigger is the entity, and
only the description mentions where it lives.

| ID (symbolic) | Name (draft) | Criteria | GS |
|---------------|--------------|----------|----|
| first_kill | First Blood | Destroy your first asteroid | 10 |
| clear_level1 | Clear Skies | Clear level 1 | 20 |
| specials_7 | Special Delivery | Destroy 7 different asteroid types | 100 |
| black_hole_survivor | Event Horizon | Survive a black-hole level (level 14 onward) without dying | 80 |
| mini_station_kill | Little Nuisance | Destroy a mini-station | 50 |
| shield_ram | Battering Ram | Destroy an enemy by ramming it with your shield active | 20 |
| shield_ram_asteroid | Icebreaker | Ram an asteroid with your shield active | 10 |
| station_destroyed | Station to Station | Destroy the enemy station (appears at level 15) | 160 |
| enemies_10 | Ace | Destroy 10 enemy ships in one game | 40 |
| nova_detonated | Nova | Detonate a nova | 60 |
| no_damage_clear | Untouchable | Clear level 9 or beyond without taking damage | 100 |
| no_secondary_level10 | Purist | Reach level 10 without using a secondary weapon | 60 |
| weapons_7 | Full Arsenal | Fire 7 different weapon types in one game | 60 |
| coop_clear | Co-Pilot | Clear a level in 2-player mode | 40 |
| kills_1000 | Millennium | Destroy 1,000 asteroids in one game | 50 |
| kills_10000_lifetime | Myriad | Destroy 10,000 asteroids (lifetime — `stats.dat`, §4) | 40 |
| score_3m | Megascore | Score 3,000,000 points in one game | 60 |
| reach_level15 | Deep Space | Reach level 15 | 40 |
| | **Total** | | **1,000** |

18 achievements ≥ the 10 minimum; max single value 160 (under the 200 cap);
`shield_ram` (funded from `coop_clear`, 60 → 40) is the fun low-stakes one —
it counts enemy ships and the mini-station, so it's earnable from level 11.
`shield_ram_asteroid` (funded from `kills_1000`, 60 → 50) is the genuinely
easy early one: shield pickups drop from level 1 and the ram already
destroys the asteroid in existing gameplay.
`score_3m` is pitched just above the best playtest run (2.7M) — a
beat-your-best skill target — and took its GS from `kills_10000_lifetime`
(100 → 40), which is time investment rather than skill.
`no_secondary_level10` (primary gun and god-mode pickups only, all the way
to level 10) took its GS from `reach_level15` (100 → 40): the
reach milestone cedes points to the skill runs, while `station_destroyed`
still pays 160 for the follow-through at level 15.
late-game items keep the "half the content" rule honest. Note `coop_clear`
assumes local 2P exists on the platform (touch builds may need a variant or
a netplay-era criteria revision). **Decision (2026-07): `coop_clear` is not
defined in the Game Center / Play portals and stays unmapped in the mobile
backends until netplay lands** — a defined-but-unearnable achievement would
break the earnability house rule, and both stores allow adding achievements
post-launch. The criteria ("Clear a level in 2-player mode") already covers
netplay co-op unchanged; its points stay reserved in the Game Center budget. `enemies_10` took its GS from
`station_destroyed` (200 → 160), which got easier when the station moved to
level 15 — the fighters are part of the same content. Despite its simple
criteria, `nova_detonated` is deceptively hard: charge pickups drop every
100 kills, a nova needs 10 charge points, and death resets charge progress
and wipes stocked novas — effectively ~1,000 kills in a single life.
Playtest showed a strong no-death run reaches 9 of 10 charges by level 9,
so it is hard but attainable and its 60 GS stands (see the playtest
findings below).

### Difficulty check before freezing

The two forces to balance:

- XR-055 requires every achievement to be **achievable**, and cert test
  057-01's steps literally include "**gain all achievements**" — a list only
  an expert can finish makes certification itself painful (plan on providing
  a cert-notes save/route for the late ones regardless).
- The "less than half the content" Critical cuts the other way: the list
  must still reach deep into the progression, so the fix is tuning numbers,
  not deleting the late achievements.

**Playtest findings (2026-07):** a competent player consistently reaches
level 9, so getting *to* the black-hole level (10) is reliable — but the
black-hole level itself is the difficulty wall. Using the dev start
(`NEWTONIA_START_GENERATION=10`), the wall has now been **passed in
testing** — black-hole levels are beatable, though not easily — and
**level 11 has been passed** the same way. A **full continuous run has
since reached level 10** from a fresh start, confirming reachability and
pinning down *why* the wall is hard: the black hole keeps accelerating
asteroids that pass near it, and while speed is clamped each step to the
absolute `max_speed` (3, `asteroid.cpp`), asteroids *spawn* at
`max_speed / radius` — so gravity can drive a large asteroid to ~20–240×
its natural speed before the clamp bites. The level fills with big,
fast-flying asteroids far harder to dodge and hit than anything in
levels 1–9. **Tuning applied (2026-07):** speed above 4× natural now
decays away (2 s time constant) once an asteroid is *outside* the
influence radius; inside the well slingshot dynamics are untouched, so
asteroids still escape (a naive always-on decay trapped them in orbit).
A 10-minute headless sim (40 asteroids, gen-10 world) shows well escapes
preserved (561 vs 932 before) while time spent outside the well above 4×
natural speed drops from 46% to 2.4% — slung asteroids cross the level
hot, then settle. **Progression change (2026-07):** the black hole itself
moved from level 10 (generation 9) to level 14 (generation 13), so a run
to the station level (15) faces exactly one black-hole level. The
findings above describe the hole at its old level-10 position;
black-hole levels need re-playtesting at 14 with both changes in. Dev-start caveat, cutting both
ways: full lives (4) at the start of the tested level, but also a bare
loadout (base gun only — no banked secondaries, upgrades, or extra lives
a real run accumulates through levels 1–9). Station fight (dev start at 14,
first attempt): **8 fighters downed** — `enemies_10` is comfortably
reachable (kills accrue game-wide and waves keep deploying) — but the
station itself survived. **Follow-up attempt: the station died easily**
once the player brought an upgraded multishot gun — the 100-hit health
pool that felt like an endurance siege at bare loadout melts under a
levelled default weapon, which a real run reaching level 15 will have.
`station_destroyed` is confirmed earnable; the loadout, not the health
pool, is the variable. (Tactical note from the deploy code, still useful
at bare loadout: a new wave only launches once the current wave is wiped
— leaving one fighter alive leashes the reinforcements while the station
is burned down. The station takes 100 hits; escort waves grow by one
fighter per redeploy, capped at 50/wave, after which wave difficulty
climbs instead.) Remaining unknown: the 10–15 stretch as one continuous run. A run to level 10 scored
~2.7M points, which (at `value ≈ 1600/radius` per kill × the
kills-per-life multiplier) implies on the order of 1,000–1,500 asteroid
kills per good run — 100 kills in one game proved trivial (reached by
level ~3), so the per-game kill achievement is pitched at 1,000
(`kills_1000`, roughly one full run) and the lifetime one at 10,000
(`kills_10000_lifetime`, a genuine long-haul across many runs). A no-death
run to level 9 yielded a 90+ multiplier and 9 of the 10 nova charges:
`nova_detonated` (~1,000 kills in one life) sits right at the edge of a
strong run — hard but genuinely attainable, so its criteria and 60 GS
stand. Decisions taken:

- GS values stay as drafted (the table above is no longer placeholder).
- `no_damage_clear` anchored at level 9 — the edge of the consistently
  reachable range, right before the wall.
- `reach_level25` re-anchored to **level 15** (`reach_level15`) so the
  progression achievement sits past the wall but not expert-only deep.
- The enemy station moved from level 21 to **level 15** (gameplay change:
  the station spawn, the 3000×3000 world-growth jump, and the intro screen
  all moved together from generation 20 to generation 14 in `glgame.cpp`),
  so the station now arrives on the same level `reach_level15` fires.
- Still open for the late items: soften criteria if needed (e.g. "damage"
  excludes shield-absorbed hits; `black_hole_survivor` already counts any
  player surviving in 2P).

### Earnability audit (2026-07)

"Possible" splits into *mechanically earnable* (no hidden gate) and
*humanly achievable* (someone can play that well). Status per achievement:

| Achievement | Gate analysis | Status |
|---|---|---|
| first_kill, clear_level1, kills_1000, shield_ram_asteroid | none — verified in play / headless | **verified** |
| score_3m | playtest hit 2.7M by level 10; a level-15 run clears 3M | earnable |
| kills_10000_lifetime | pure accumulation (~7–10 good runs) | earnable |
| specials_7 | counts the 7 killable types: normal, invisible, teleporting (window), quantum (observed), tough, armoured, phasing — all die to ordinary weapons. Reflective/invincible are excluded (they die only to god mode, asteroid.cpp:63) and count as bonus kills, not requirements. Lifetime mask spans games | earnable |
| coop_clear | needs a second controller/player only | earnable |
| mini_station_kill | single shot from level 11 | earnable |
| shield_ram | shield pickup (1.25%/kill) + any enemy from level 11 | earnable |
| no_damage_clear, black_hole_survivor | skill-only, no mechanical gate; playtest shows level 9 clears consistently. The black-hole wall (accelerated asteroids) prompted two changes: slingshot-speed decay, and the hole moving from level 10 to 14 — black_hole_survivor now gates on level 14 | earnable, hard |
| nova_detonated | charge counter **and** charges reset on death → ~1,000 kills in one life; playtest reached 9/10 charges | earnable, at the edge |
| **weapons_7** | **coupled to nova_detonated**: firing a nova is the 7th kind, so it embeds the same one-life feat, plus drop RNG — P(≥1 god-mode drop) ≈ 63% by 400 kills, 92% by 1,000, 98% by 1,500 (other pickups ≈100%; the fired-mask spans lives, only the nova leg is single-life) | earnable; capstone-hard, review GS |
| station_destroyed, enemies_10 | **verified in dev-start play**: station dies easily to an upgraded multishot gun (the bare-loadout siege was the outlier); 8+ fighters downed in a single fight | **verified** |
| reach_level15 | the black-hole wall has been passed in dev-start testing (hard but doable), levels 10 and 11 individually cleared; the stretch is now easier — the hole moved to level 14, leaving one black-hole level before the station; still unproven: chaining 10–15 in one run | **needs playtest** (de-risked) |

**Playtest tooling:** beta builds honour `NEWTONIA_START_GENERATION=N`
(with `NEWTONIA_BETA=1`) — a new game starts at generation N with the
correct world size and hazards (mini-station ≥10, black hole ≥13, station
≥14), so the wall and the station fight can be practised directly. It
marks the game as cheated, so it can never launder achievements; use it to
answer the remaining "needs playtest" row (the continuous 10–15 run). The
station fight itself has proved out — no GS re-pitch needed there.

## 6. Work items (this repo)

1. `achievements.h/.cpp` — seam + no-op default backend.
2. Hooks at the §3 call sites in `GLGame`/`Ship`, using the symbolic IDs
   from the §5 table.
3. `stats.dat` lifetime stats via `SaveStorage` (Roaming), with the
   local-player attribution rule.
4. The XR-057 cheat-suppression flag (game-scoped `cheated`, set by
   skip-level and time-scale keys, cleared only on new game, persisted in
   the savegame) gating all unlocks in the shared layer.
5. One backend at a time against the same seam: Steamworks (**done**),
   Game Center (**done** — `game_center_achievements.mm` plus the shared
   pending journal `achievement_journal.h/.cpp`), Google Play Games
   Services (**done** — `play_games_achievements.cpp` +
   `PlayGamesAchievements.java`; Play Console config pending).

The Xbox backend (GDK Achievements Manager wiring, Partner Center config,
sandbox testing) is tracked in the private GDKX mirror and consumes this
work as-is.
