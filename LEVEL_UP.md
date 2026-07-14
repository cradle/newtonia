# Google Play "Level Up" Program — Requirements & Compliance Tracker

Working reference for evaluating Newtonia against Google Play's revamped **Level Up**
program. Captures every guideline, its deadline, its exemptions, and Newtonia's current
state, so we can go through them one at a time.

- **Official guidelines:** https://developer.android.com/games/guidelines (updated 2026-07-10)
- **Program overview / Console:** https://play.google.com/console/about/levelup/
- **Help Center (Level Up+):** https://support.google.com/googleplay/android-developer/answer/16501431
- **Source email:** Google Play, "Metonymous: Here's what's changing with Level Up" (2026-07-13)
- **Audit basis:** codebase state as of 2026-07-14 (branch `claude/vulcan-android-support-rh59k3`)

---

## 0. Why this matters (and when it doesn't)

The **only** reward for Level Up compliance is qualifying for the **program rate card — a
lower Play service fee on revenue.** There is no other benefit.

> ⚠️ **Newtonia currently has no billing / IAP integration** (`android/app/build.gradle`
> pulls in `play-services-games-v2` only — no `com.android.billingclient`). If the game
> stays **free with no purchases, the rate card is worth $0** and none of these
> requirements are worth implementing for Level Up's sake alone. Several of them (cloud
> save, large-screen support, mouse input) are still worthwhile on their own merits.

**Decision gate before doing any of this work:** is Newtonia going to be monetized on
Play? If no → this whole program is moot. If yes/maybe → proceed.

### Key dates
| Date | Milestone |
|---|---|
| **Sept 2026** | Play Console submission opens (verify compliance, unlock rate card) |
| **Sept 30, 2026** | Eligibility opens; Rewards (2 single-use), form-factor distribution (mobile/foldable/tablet), simultaneous-release requirement all begin |
| **March 1, 2027** | Rewards (1 repeatable) required; Googlebook form factor required |
| **Sept 2027** | XR / TV / Auto form factors required |

### How compliance is verified
- **Reference devices:** Google's test matrix, grouped by SoC (Tensor G5/G4, Snapdragon
  8 Elite, MediaTek Dimensity, Exynos, etc.) across phones/tablets/foldables.
- **Self-certification:** validate **one device per SoC family** — not every model. E.g.
  one Snapdragon 8 Elite phone certifies Galaxy S25 / OnePlus 13 / Xiaomi 15 together.
- Submit through Play Console (from Sept 2026) to certify all guidelines at once.

### Status legend
- ✅ **Done** — implemented and (believed) compliant
- ⚠️ **Partial** — some of it exists; gaps noted
- ❌ **Missing** — not implemented
- 🅴 **Exempt (likely)** — an exemption plausibly applies; confirm in Console

---

## Pillar 1 — Provide a consistent gamer experience

### 1.1 Play Games Services v2 — Platform Authentication ✅
- **Requirement:** Integrate PGS v2 SDK; initialize at startup.
- **Exemptions:** Games primarily targeting players **under 13** (also exempts everything
  else in this pillar).
- **Newtonia:** **Done.** `play-services-games-v2:20.1.2` (`android/app/build.gradle:59`),
  APP_ID meta-data (`AndroidManifest.xml:28-29`), auto sign-in via `PlayGamesSdk.initialize`
  in `PlayGamesAchievements.java`, native init at `android_main.cpp:299`.

### 1.2 Achievements ✅
- **Requirement:** Minimum **10** achievements (40+ recommended); unique names/descriptions.
- **Exemptions:** under-13; games lacking "clear events, progression, or player
  accomplishment." **Newtonia does NOT qualify** for the no-progression exemption — it has
  scores, levels, enemy defeats, item collection (all explicitly disqualifying).
- **Newtonia:** **Done.** 17 achievements with real console IDs populated in
  `android/app/src/main/res/values/games-ids.xml` (project `717199808901`); symbolic→resource
  mapping in `play_games_achievements.cpp:57-75`; `coop_clear` intentionally omitted pending
  netplay.
- **Note:** CLAUDE.md:387 says IDs "stay commented out until the Play Console project
  exists" — this is **stale**; IDs are populated. Update CLAUDE.md.
- **Residual gap:** `minifyEnabled false` (`build.gradle:39`) means the ProGuard keep-rules
  for the JNI bridge are untested; `versionName` defaults to `0.0.1`.

### 1.3 Game Stats ❌
- **Requirement:** Submit **≥5 repetitive stats + ≥1 player-progression stat** (via PGS
  `EventsClient`); powers quests/leagues.
- **Exemptions:** under-13; games lacking progression. **Newtonia does NOT qualify**
  (has scores/levels/XP-like progression).
- **Newtonia:** **Missing.** Local `stats.h/cpp` → `stats.dat` exists but never touches any
  Play API. Would need `EventsClient` wiring + event IDs in the Console.

### 1.4 Rewards ❌ / 🅴
- **Requirement:** ≥2 single-use Play Games Reward offers by **Sept 30, 2026**; ≥1
  repeatable by **March 1, 2027**.
- **Exemptions:** under-13; **paid games** ("Players expect the full game experience after
  their initial purchase").
- **Newtonia:** **Missing.** 🅴 if shipped as a paid game.

### 1.5 Cloud Save ❌
- **Requirement:** Save player state off-device (PGS Saved Games / Snapshots, ≤3 MB, or any
  cloud provider); retrieve on startup; conflict-resolution policy.
- **Exemptions:** guest-only games / games with **no** saved state across sessions; saves
  exceeding 3 MB where third-party cloud is cost-prohibitive. **Newtonia's exemption is
  borderline** — it *does* persist state, so guest-only likely does not apply cleanly.
- **Newtonia:** **Missing.** Android persistence is local only: `savegame.cpp:16-17` writes
  `savegame.dat` to `SDL_GetPrefPath`; `allowBackup="false"` (`AndroidManifest.xml:21`)
  disables even Android Auto Backup. Web IDBFS is web-only, not cross-device with Android.
- **Effort note:** a binary savegame format already exists to serialize into a Snapshot.

### 1.6 Sidekick ❌
- **Requirement:** Add the Play Games Sidekick overlay.
- **Exemptions:** under-13; accepted blocking issue.
- **Newtonia:** **Missing.** No references anywhere. Low value for this game.

---

## Pillar 2 — Expand your reach across all screens

### 2.1 Large-screen optimization (resize / rotate) ⚠️
- **Requirement:** Handle system bars, display cutouts, config changes. Support anchor
  aspect ratios (landscape 4:3/16:10/21:9; portrait 3:4/10:16/9:21). No letterboxing except
  under defined conditions.
- **Exemptions:** non-flagship aspect ratios; single-orientation layout vs. device physical
  orientation.
- **Newtonia:** **Partial.** Config changes handled without restart
  (`configChanges="orientation|screenSize|smallestScreenSize|screenLayout|density|..."`,
  `AndroidManifest.xml:36`); runtime resize wired (`android_main.cpp:307-309, 363-373`).
  **Gaps:** orientation locked to `sensorLandscape` (landscape-only, no portrait); **no
  `android:resizeableActivity`**; no `<layout>` / multi-window / free-form declarations.

### 2.2 Precision input — keyboard / mouse / controller ⚠️
- **Requirement:** Fully playable with **controller, keyboard, AND mouse** on all supported
  devices.
- **Exemptions (by core mechanic):** GPS/location, ambient sensors, kinetic/AR camera,
  gyro aiming, intense rhythm/multi-touch → controller & K&M not required; gestural drawing
  / drag-and-drop point-and-click → K&M required. **Newtonia matches none of these** → all
  three input types expected.
- **Newtonia:** **Partial.** Controller ✅ (`SDL_INIT_GAMECONTROLLER`, `android_main.cpp:194,
  288-294, 377`); physical keyboard ✅ (`android_main.cpp:322-337`); **mouse ❌** — no
  `SDL_MOUSEMOTION` / `SDL_MOUSEBUTTONDOWN` handling.

### 2.3 Google Play Games on PC ⚠️
- **Requirement:** Distributed on GPGoP (emulator-playable or Native PC EAP).
- **Exemptions:** form-factor constraints that degrade quality (AR/location, sensors, 360°
  body rotation); title-level IP-licensing limits; paid mobile↔PC price differential.
- **Newtonia:** **Partial.** Positives: `x86_64` ABI built (`build.gradle:26`), `touchscreen
  required=false` (`AndroidManifest.xml:15-16`), keyboard+controller work. Negatives: no
  mouse input, landscape-locked, no `resizeableActivity`, PC input/window declarations absent.

### 2.4 Form-factor distribution ⚠️
- **Requirement:** Distribute across Mobile + Foldables + Tablets (Sept 30, 2026);
  Googlebook (Mar 1, 2027); XR/TV/Auto (Sept 2027).
- **Exemptions:** hardware below your min mobile spec; form factor unavailable in offered
  regions.
- **Newtonia:** **Partial.** ABIs good (`arm64-v8a`, `armeabi-v7a`, `x86_64`). **No
  large-screen / tablet / foldable declarations**; `uses-feature` only lists GLES 2.0
  (required), `audio.low_latency` (optional), `touchscreen` (optional).

### 2.5 Title availability (simultaneous release) ❓
- **Requirement (from Sept 30, 2026):** launch on Android form factors simultaneously with
  comparable non-Android platforms.
- **Exemptions:** platform-suitability degradation; 15-day launch grace (30 with manual
  review); does not apply in the EEA.
- **Newtonia:** N/A unless there's a parallel non-Android launch to sync with.

---

## Pillar 3 — Deliver stable and smoother gameplay sessions

### 3.1 Stability — crashes & ANRs ⚠️
- **Requirement:** Crashes **<1%** avg (test devices) / **<2%** (4GB+ RAM); ANRs **<2%** /
  **<3%**.
- **Exemptions:** **NONE** ("There are no exemptions from this guideline.")
- **Newtonia:** **Partial / unverified.** No crash-reporting SDK (no Crashlytics/Firebase/
  Sentry/Breakpad) — relies solely on Play's Android Vitals. JNI bridge is defensively coded
  (`play_games_achievements.cpp:96-101`); foreground catch-up guard avoids ANR spiral
  (`android_main.cpp:388-393`). **Action:** check Android Vitals once there's install base;
  no code change strictly required to comply, but no visibility into field crashes either.

### 3.2 Performance — frame rate ✅
- **Requirement:** Target 60 FPS on reference devices — avg **≥55**, P90 **≥50**, P99 **≥30**.
- **Exemptions:** games not doing 60 FPS on other platforms; HWUI/Composer rendering;
  frames only pushed on interaction; **not required on tablets/foldables** (thermal).
- **Newtonia:** **Done (expected).** 8 ms fixed timestep (`glgame.h:110`) + accumulator +
  vsync (`android_main.cpp:231`). A 2D vector shooter trivially clears 60 FPS on reference
  phones.

### 3.3 Memory ❓ (criteria forthcoming)
- **Requirement:** Android 17+ will require memory optimization aligned to platform
  standards; detailed criteria "coming soon."
- **Exemptions:** not yet specified.
- **Newtonia:** No instrumentation; no `largeHeap`, no `onTrimMemory`. Small footprint
  (2D, WAV audio). Revisit when criteria publish.

### 3.4 Vulkan graphics API ❌ → use ANGLE
- **Requirement:** Vulkan must be the **primary** graphics API on conformant devices
  (DEQP ≥ 20240301). Applies to Unity 2021+, UE 4.25+, and **all other games — "unless
  opting for ANGLE on Android 17+."**
- **Exemptions:** HWUI/SKIA-rendered games; WebGPU games; accepted Vulkan blocking issue;
  developer demonstrating ~30% CPU-instruction reduction by other means.
  **There is NO blanket exemption for 2D / low-graphical-intensity games.**
- **Newtonia:** **Missing (uses GLES2).** Requests OpenGL **ES 2.0** context
  (`android_main.cpp:201-203`), `<GLES2/gl2.h>`, links `GLESv2`/`EGL`, manifest declares
  `glEsVersion=0x00020000 required=true`. minSdk 21 chosen for guaranteed GLES2.
- **Recommended path — ANGLE, not a native Vulkan port:** the guideline itself accepts
  **ANGLE on Android 17+**, which translates GLES2 → Vulkan at runtime with **zero
  game-code change.** The project already depends on ANGLE for the Xbox/GDK port. A native
  Vulkan backend behind the `gles2_compat` shim is technically possible but large, buys no
  visible benefit for a 2D vector game, and adds a second rendering path to maintain
  forever. **Avoid the native port; adopt ANGLE if/when this guideline must be met.**

---

## Build / manifest facts (reference)
- `android/app/build.gradle`: **compileSdk 35**, **minSdk 21**, **targetSdk 35**; ABIs
  `arm64-v8a, armeabi-v7a, x86_64`; NDK 26.3.11579264 / CMake 3.22.1; AGP 8.2.0;
  `minifyEnabled false`; `versionName 0.0.1`.
- `AndroidManifest.xml`: permission `VIBRATE`; `uses-feature` GLES2 (required),
  `audio.low_latency` (optional), `touchscreen` (optional); `screenOrientation`
  `sensorLandscape`; **no** `resizeableActivity`; **no** `<supports-screens>`;
  `allowBackup="false"`; `hardwareAccelerated="true"`; PGS APP_ID meta-data.

---

## Summary tracker

| # | Guideline | Deadline | Status | Exemption path | Effort |
|---|---|---|---|---|---|
| 1.1 | PGS v2 auth | Sept '26 | ✅ Done | under-13 | — |
| 1.2 | Achievements | Sept '26 | ✅ Done | no-progression (N/A) | — |
| 1.3 | Game Stats | Sept '26 | ❌ Missing | no-progression (N/A) | Medium |
| 1.4 | Rewards | Sept '26 / Mar '27 | ❌ / 🅴 | **paid game** | Medium |
| 1.5 | Cloud Save | Sept '26 | ❌ Missing | guest-only (borderline) | Medium |
| 1.6 | Sidekick | Sept '26 | ❌ Missing | under-13 | Medium |
| 2.1 | Resize/rotate | Sept '26 | ⚠️ Partial | aspect-ratio edge | Low |
| 2.2 | Keyboard/mouse/controller | Sept '26 | ⚠️ Partial (no mouse) | mechanic-based (N/A) | Low |
| 2.3 | Play Games on PC | Sept '26 | ⚠️ Partial | quality degradation | Low–Med |
| 2.4 | Form-factor distribution | Sept '26+ | ⚠️ Partial | below-spec / region | Low |
| 2.5 | Simultaneous release | Sept '26 | ❓ N/A | EEA / suitability | — |
| 3.1 | Crashes/ANRs | Sept '26 | ⚠️ Verify | **none** | Monitor |
| 3.2 | 60 FPS | Sept '26 | ✅ Done | tablets/foldables | — |
| 3.3 | Memory | Android 17+ | ❓ TBD | TBD | TBD |
| 3.4 | Vulkan | Sept '26 | ❌ → ANGLE | HWUI/WebGPU/CPU-30% | Low (ANGLE) |

**Cheapest high-value cluster** (self-contained, low risk): **2.1 resize/rotate + 2.2 mouse
input** — mostly manifest changes plus SDL mouse-event handling that SDL already delivers.

**Skip:** native Vulkan port (use ANGLE), Sidekick (low value), Rewards (exempt if paid).
