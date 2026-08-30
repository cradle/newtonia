# Google Play 2027 technical quality requirements — assessment & plan

Google announced two new Play quality requirements (2026-08-26 blog post,
"App quality: memory optimization and secure onboarding"):

1. **Memory usage + code optimization** — enforced **February 2027**, all
   apps and games. Three metered thresholds (Android Vitals, 90th
   percentile over the install base): dynamic memory (anonymous RSS +
   swap) per app state and device-RAM tier, bitmap memory in
   background/cached states, and DEX code optimization coverage (≥ 25%
   each for optimization / shrinking / obfuscation, R8 or equivalent).
2. **Secure device migration ("zero-tap sign-in")** — enforced **April
   2027**: apps with sign-in must restore sign-in state on a new device
   via the Android Restore Credentials API. **Games are exempt** (strongly
   encouraged only; Google says dedicated gaming-auth guidance comes
   in 2027).

Non-compliance costs visibility and publishing capability, not an
immediate delisting. Sources: the announcement
(android-developers.googleblog.com/2026/08/app-quality-memory-optimization-secure-onboarding.html)
and the threshold tables in Play Console Help answer 17492799.

## Where Newtonia stands

Newtonia's Android build is a native NDK game (SDL2 + GLES2, everything in
`libnewtonia.so`) with a deliberately tiny Java layer: SDL's glue classes
plus three of our own (`NewtoniaActivity`, `PlayGamesAchievements`,
`PlayGamesIdentity`) and four small libraries (play-services-games-v2,
androidx floors, kotlin-bom, installreferrer). That shape decides almost
everything below.

### 1. Dynamic memory (anon RSS + swap) — NO RISK, monitor only

The games thresholds are generous at our scale: the tightest cell is
2.25 GB foreground / 2.0 GB background on 4 GB devices (p90). Newtonia is
a 2D vector game — meshes, a few WAVs, no textures to speak of — and its
whole native heap + GL footprint sits orders of magnitude under that.
There is no in-app state that grows unboundedly (the world grows per
generation but object counts stay in the hundreds).

**Action:** none in code. When the new dynamic-memory metrics appear in
Android Vitals, eyeball them once after the next release. A local
baseline any time: `adb shell dumpsys meminfo org.newtonia` after a few
minutes of late-game play (the "Anon RSS" the requirement meters is
roughly Private Dirty + swap there).

### 2. Bitmap memory — NO RISK

The thresholds (200 MB background, 400 MB cached, p90) meter
`android.graphics.Bitmap` allocations. Newtonia allocates none — rendering
is SDL/GLES from native code; the only bitmaps in the process belong to
Play Games UI popups and are transient. Nothing to do.

### 3. DEX optimization — EXEMPT BY SIZE, but enable R8 anyway

The 25% coverage floors apply to **apps with > 10 MB DEX and games with
> 50 MB DEX**. Our entire DEX (SDL glue + three classes + the four
libraries, unshrunk) is single-digit MB — as a game we are nowhere near
the 50 MB cutoff, so the requirement cannot bite as long as the
dependency list stays this shape. Verify once rather than assume:
`unzip -l app-release.aab 'base/dex/*'` on the next deploy artifact.

R8 is enabled regardless (`minifyEnabled true` in
`android/app/build.gradle`'s release block, on the optimizing default
rules file): it is insurance against the dependency tree growing past a
cutoff unnoticed, it shrinks the download, and Play Console now surfaces
"DEX optimization insights" on every AAB upload that would otherwise nag
forever. The keep rules in `android/app/proguard-rules.pro` are what
make the flip safe — see the traps below before touching either file.

**R8 enablement checklist**:

- [x] Keep rules cover every class reached only through JNI:
      `org.libsdl.app.**`, `NewtoniaActivity`, `PlayGamesAchievements`,
      and — it was missing —
      `PlayGamesIdentity` (resolved by name from
      `play_games_identity.cpp`; without the rule R8 strips it and
      identity fails SOFT: names degrade to role labels online, no
      leaderboard attestation, nothing says why). The installreferrer
      library is pinned too (its consumer rules should suffice, but the
      deferred-deep-link path also fails soft).
- [x] `minifyEnabled true` for release. `shrinkResources` left OFF —
      see the resource trap below.
- [x] Boot gate in CI: `android.yml` builds the minified release APK
      debug-signed (`assembleRelease -PdebugSign` — the gradle property
      attaches the debug signing config so the APK installs; deploy's
      injected release signing is untouched) and the emulator selftest
      runs twice on the one booted AVD — debug APK (the TLS gate as
      always), then the minified APK (the R8 boot gate). The minified
      APK is also uploaded as the `newtonia-release-minified` artifact
      for sideloading.
- [x] Device gate — verified on a real device 2026-08-30 (maintainer,
      local R8 `-PdebugSign` build under a console-registered cert):
      Play Games sign-in works, the attested name shows in an online
      lobby, and a leaderboard upload attests and lands (against the
      BETA board — production refused the dev build's non-canonical
      season with SEASON NOT ACCEPTED first, which is the prod season
      whitelist working as designed, not an R8 symptom). The CI
      artifact separately passed an on-device boot/gameplay smoke.
      Achievement unlocks weren't exercised on their own;
      `PlayGamesAchievements` predates this work in the keep rules and
      rides the same bridge machinery the identity tests proved.
      Testing the PGS paths needs a build whose cert the console knows:
      a local `-PdebugSign` build under a registered personal debug
      keystore, an internal-track dispatch (real release key), or — once
      the secret exists — the CI artifacts themselves (next section).
- [ ] On the first Play upload after the flip: confirm the Play Console
      shows deobfuscated crash traces / accepts the mapping (AGP embeds
      `BUNDLE-METADATA/…/proguard.map` in the AAB automatically).

**Shared debug keystore** (`DEBUG_KEYSTORE_BASE64` repo secret →
`android/debug.keystore`, decoded by android.yml; gitignored): one debug
signing cert across every CI run, so PR artifacts install over each
other and — because the cert is registered with Play Games — can sign in
and attest, closing the gap where `newtonia-release-minified` could
smoke-test everything except the fail-soft PGS paths. The keystore is
SECRET-HELD, mirroring the release key's handling, and that is the load-
bearing property: a first attempt COMMITTED the keystore to this public
repo, and the security review killed it (2026-08-30) — a world-readable
private key authorized on the production Google project lets anyone mint
genuinely-attesting builds (a legitimacy veneer for repackaged clients,
and a permanent weakening of the board's accountability premise). The
cert that was briefly registered for that committed key
(SHA-1 21:3A:8D:6F:...:C3:86, still visible in the PR-branch history)
is DE-REGISTERED and must never be authorized again. `signingConfigs`
in `app/build.gradle` only overrides the debug config when the decoded
file exists, so builds without the secret (fork PRs, fresh clones) fall
back to the ordinary per-machine keystore — they build and boot fine,
their certs just can't sign in to PGS.

One-time setup (maintainer, locally — the key must never transit
anything but the GitHub secret):

    keytool -genkeypair -v -keystore debug.keystore -storepass android \
        -keypass android -alias androiddebugkey -keyalg RSA -keysize 2048 \
        -validity 10950 -dname "CN=Newtonia CI Debug,O=Newtonia,C=AU"
    keytool -list -v -keystore debug.keystore -storepass android \
        -alias androiddebugkey | grep SHA1:      # register this SHA-1
    base64 -w0 debug.keystore                    # -> DEBUG_KEYSTORE_BASE64

Register the SHA-1 as an **Android** OAuth client for `org.newtonia`
(Play Console → Play Games Services → Setup and management →
Configuration → Credentials; personal debug certs registered the same
way coexist fine — entries are additive, and each is revocable there
alone). The game-server (web) OAuth client is cert-independent and
untouched. Rotation = new keystore, replace the secret, register the new
SHA-1, drop the old entry. Deliberately NOT in
`web/site/.well-known/assetlinks.json` — App Links interception from
test builds has no testing payoff; add it there only as an explicit
decision.

**Resource trap:** both Play Games bridges read their string resources by
name at runtime — `getIdentifier("play_games_oauth_client_id"…)` in
`PlayGamesIdentity.java`, and every achievement id in
`PlayGamesAchievements.java` (`games-ids.xml`). The resource shrinker
cannot see name-based lookups, so `shrinkResources true` would strip
those strings and achievements/attestation would again fail soft (a
logcat warning is the only symptom). Either keep `shrinkResources` off
(the win is trivial at our resource count — a handful of launcher
icons), or add a `res/raw/keep.xml` with
`tools:keep="@string/game_services_project_id,@string/play_games_*,@string/achievement_*"`
first.

### 4. Zero-tap sign-in (April 2027) — EXEMPT, nothing to build

Games are exempt, and Newtonia has no in-app credentials to restore
anyway: the only sign-in is Play Games Services v2 **automatic** sign-in
(`GamesSignInClient`), which already rides the Google account to a new
device — the migrated user opens the game signed in without any tap
today. The Restore Credentials API targets apps holding their own
passkeys/passwords/federated tokens; we hold none.

**Action:** none now. Re-check when Google publishes the promised
"dedicated guidance and tailored solutions for complex gaming
authentication use cases" in 2027 — if the exemption narrows, our answer
is still "PGS v2 automatic sign-in", which is Google's own recommended
path for games.

**Adjacent, deliberately out of scope:** the manifest sets
`allowBackup="false"`, so device-to-device migration carries the Play
Games identity but NOT local `savegame.dat` / `stats.dat` /
`preferences`. That is today's intended behaviour (local files also back
the leaderboard's best-run slots and the cheat flag — auto-backup
restoring them un-audited is its own can of worms). If save migration is
ever wanted, the path is PGS Saved Games (or scoped backup rules that
exclude the leaderboard-sensitive files), designed on its own branch —
the April requirement does not ask for it.

## Timeline

| When | What |
|------|------|
| Done | This assessment; `PlayGamesIdentity` keep rule; the R8 flip + CI boot gate; device gate verified 2026-08-30; secret-held shared-keystore wiring |
| Once (maintainer, local) | Mint the shared debug keystore, create the `DEBUG_KEYSTORE_BASE64` repo secret, register its SHA-1 (commands above) — until then CI artifacts carry throwaway certs |
| Next deploy artifact | Read actual DEX size off the AAB; confirm "games, < 50 MB" exemption holds; confirm Play deobfuscates traces |
| After next release, once Vitals ships the new metrics | Confirm memory/bitmap panels are green; then ignore unless alerted |
| 2027, when Google's gaming-auth guidance lands | Re-check the sign-in exemption still covers PGS-only games |
