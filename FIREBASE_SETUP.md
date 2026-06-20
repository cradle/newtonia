# Firebase / Google Analytics setup (iOS & Android)

The web pages use the gtag.js snippet (`G-03BDC6CK12`). Native apps can't use
gtag — Google Analytics for apps is delivered through **Firebase Analytics**,
which needs a per-app config file and the Firebase SDK linked in.

The code wiring is already in place. To activate analytics you need to create
the GA4 **app data streams** (which sets up Firebase) and drop in the two config
files. None of this is secret in the cryptographic sense — the IDs/keys in these
files are client identifiers meant to ship inside the app.

## 1. Create the Firebase project / GA4 app streams

1. Go to <https://console.firebase.google.com> and create a project (or reuse an
   existing one). Link it to the GA4 property that owns `G-03BDC6CK12` when
   prompted, so app + web data land in the same property.
2. Add an **Android app**: package name `org.newtonia`. Download
   `google-services.json`.
3. Add an **iOS app**: bundle ID `cc.gfm.Newtonia`. Download
   `GoogleService-Info.plist`.

(Equivalently, in GA4 **Admin → Data streams → Add stream → Android/iOS app** —
this walks through the same Firebase registration.)

## 2. Android

Drop the downloaded file at:

```
android/app/google-services.json
```

That's it. The Gradle wiring is already done:

- `android/build.gradle` declares the `com.google.gms.google-services` plugin.
- `android/app/build.gradle` adds the Firebase BoM + `firebase-analytics`, and
  **only applies** the google-services plugin when `google-services.json` is
  present (so the build stays green before the file is added).
- `NewtoniaActivity.onCreate()` touches `FirebaseAnalytics.getInstance(this)` to
  ensure init; it's wrapped in try/catch so the app still launches if the file
  is missing.

For CI to produce an analytics-enabled APK, `google-services.json` must be
available at build time — either commit it to the repo (standard for client
config) or inject it in `.github/workflows/android.yml` from a repo secret
before `./gradlew assembleDebug`.

## 3. iOS

iOS Firebase is wired for **Xcode device / App Store builds** (the CI workflow
hand-compiles a Simulator binary and intentionally stays Firebase-free).

1. Add `GoogleService-Info.plist` to the Xcode project
   (`ios/Newtonia-iOS.xcodeproj`): drag it into the project navigator, ensure
   "Copy items if needed" and the Newtonia target membership are checked so it's
   bundled into the `.app`.
2. Add the Firebase SDK via **Swift Package Manager**:
   File → Add Package Dependencies → `https://github.com/firebase/firebase-ios-sdk`
   → add the **FirebaseAnalytics** product to the Newtonia target.
3. Define the `USE_FIREBASE` compile flag for the target
   (Build Settings → *Other C Flags* / *Preprocessor Macros* → `USE_FIREBASE=1`).
   This enables the `@import FirebaseCore;` + `[FIRApp configure];` block already
   present in `ios_main.mm`.

Without `USE_FIREBASE`, the Firebase code is compiled out — which is why the
existing `ios.yml` Simulator build is unaffected.

## What you get

Once configured, automatic events flow with no extra code: `first_open`,
`session_start`, `screen_view`, `user_engagement`, plus device/OS/geo
breakdowns and active-user/retention reports in the GA4 console. Custom events
can be added later via `FirebaseAnalytics`/`FIRAnalytics` if desired.
