# Universal / App Link association files

These make a tapped `https://newtonia.metonymous.com/join?code=XXXX` link open
the native app directly instead of the browser (see `invites.h`, the `/join`
landing page, and the platform handlers in `ios_universal_link.mm` /
`NewtoniaActivity.java`). They are served static from the site root by
GitHub Pages (`make web` copies this whole folder into `web/dist/.well-known`).

Both are **populated with real values and served live** from master. A
wrong or missing value breaks nothing visibly — a tapped link just falls
through to the browser game (`/play/?code=`) — so verify on-device after
any change. The sections below are the runbook for re-deriving each value.

## `apple-app-site-association` (iOS Universal Links)

Replace `TEAMID` in the `appIDs` entry with the Apple Developer **Team ID**
(the same value `deploy-ios.yml` substitutes into `TEAM_ID_PLACEHOLDER` in
`ios/Entitlements.plist`, read from the provisioning profile). Final form:
`ABCDE12345.cc.gfm.Newtonia`.

Also required (portal side, one-time):
- Enable the **Associated Domains** capability on the `cc.gfm.Newtonia` App ID
  and regenerate the provisioning profile.
- The file must be served over HTTPS with a valid cert, **no redirect**, and
  no `.json` extension (both already true on GitHub Pages).

Apple's CDN caches this file aggressively, so allow time after changes.

## `assetlinks.json` (Android App Links)

Carries the app's signing certificate **SHA-256 fingerprints**
(colon-separated hex, `AB:CD:…`; multiple allowed — append, never replace).
**Field-verified 2026-09-14 on a Play-delivered public install**
(`adb shell pm list packages -i org.newtonia` →
`installer=com.android.vending`): the installed package reports the
array's second fingerprint and `pm get-app-links` answers `verified`, so
the certificate Google Play signs public installs with (Play App Signing)
is already listed — no further fingerprint is needed. To read the store's
certificate without a device: Play Console → the app → **Test and
release**, scroll to its **Setup** subsection → **App integrity** → App
signing (the console redesign nested Setup under Test and release; there
is no top-level Setup any more).

The `package_name` is `org.newtonia` (matches `NewtoniaActivity`). Android
fetches this at install to verify the `autoVerify` intent-filter in
`AndroidManifest.xml`; verify status with
`adb shell pm get-app-links org.newtonia`.
