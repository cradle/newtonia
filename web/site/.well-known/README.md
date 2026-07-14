# Universal / App Link association files

These make a tapped `https://newtonia.metonymous.com/join?code=XXXX` link open
the native app directly instead of the browser (see `invites.h`, the `/join`
landing page, and the platform handlers in `ios_universal_link.mm` /
`NewtoniaActivity.java`). They are served static from the site root by
GitHub Pages (`make web` copies this whole folder into `web/dist/.well-known`).

Both carry a **placeholder that must be filled with a real value before the
native deep links work** — until then, a tapped link falls through to the
browser game (`/play/?code=`), which still works everywhere.

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

Replace `REPLACE_WITH_PLAY_APP_SIGNING_SHA256` with the app's release signing
certificate **SHA-256 fingerprint**. With Play App Signing, copy it from
Play Console → the app → **Setup → App integrity → App signing key
certificate** (`SHA-256 certificate fingerprint`), formatted as
colon-separated hex (`AB:CD:…`). Multiple fingerprints are allowed (e.g. an
upload key too) — add them to the array.

The `package_name` is `org.newtonia` (matches `NewtoniaActivity`). Android
fetches this at install to verify the `autoVerify` intent-filter in
`AndroidManifest.xml`; verify status with
`adb shell pm get-app-links org.newtonia`.
