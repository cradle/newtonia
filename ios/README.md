# Newtonia – iOS

## Building & running on a device from the command line

No Xcode GUI project setup needed — `make ios` drives the XcodeGen
project entirely from the CLI. One-time setup on the Mac:

```bash
brew install xcodegen
./build_sdl_deps_ios.sh device       # static SDL2 + SDL2_mixer -> sdl-libs-ios/
./build_netplay_deps_ios.sh device   # static MbedTLS + libdatachannel -> netplay-libs-ios/
```

plus one GUI step that cannot be scripted: open Xcode → Settings →
Accounts and sign in the Apple ID once (automatic signing then mints and
refreshes the development profile from the command line via
`-allowProvisioningUpdates` — the team defaults to `4RWPRHJG6D`,
override with `IOS_TEAM=`). Then, from the repo root:

```bash
make ios           # signed device .app (Debug; IOS_CONFIG=Release for -O3)
make ios-install   # build + install + launch on the connected device
```

The device needs Developer Mode enabled (Settings → Privacy & Security)
and to be plugged in / paired; `make ios-install` auto-picks the first
connected device, or set `IOS_DEVICE=<name-or-udid>`. Debug builds sign
with `EntitlementsDev.plist` (Game Center sandbox, universal links, and
the multicast entitlement for LAN discovery), and netplay defines/libs
are passed automatically — the manual Build Settings wiring that used to
be documented here is gone. Audio is bundled by the project itself (a
folder resource in `project.yml`), so the .app is complete as signed.

## Running in the iOS Simulator

Download the `Newtonia-iOS-Simulator.zip` artifact from the latest GitHub Actions build and unzip it to get `Newtonia.app`.

```bash
# 1. Boot the simulator
xcrun simctl boot "iPhone 14"

# 2. Open the Simulator app to see it
open -a Simulator

# 3. Install and launch
xcrun simctl install booted Newtonia.app
xcrun simctl launch booted cc.gfm.Newtonia
```

## Requirements

- macOS with Xcode 14 or later
- iOS Simulator runtime (installed via Xcode)
- The binary is a universal fat binary (arm64 + x86_64), so it runs on both Intel and Apple Silicon Macs

## Netplay (M3-4)

CI compiles the simulator build netplay-on (see .github/workflows/ios.yml —
it hand-builds the source list and deps, not the xcodeproj). `make ios`
passes the netplay define, header path, and static archives to xcodebuild
automatically (the same build-settings hand-off deploy-ios.yml uses), so
no Build Settings editing is needed — just run
`./build_netplay_deps_ios.sh device` once. A GUI Xcode build through the
bare project (without those settings) still builds — the netplay TUs
compile empty and the menu simply hides ONLINE.

### Testing a TestFlight build against the beta signaling worker

A stock build talks to the **production** signaling worker (the baked
`SIGNAL_URL_DEFAULT` in `net_signal.cpp`). To test a real-device build
without touching production, run **deploy-ios** from the Actions tab with the
**`signal_worker: beta`** input — it compiles `-DNEWTONIA_SIGNAL_BETA=1`, so
the TestFlight build points at `newtonia-signal-beta` (auto-deployed on master
pushes touching `signal/**`). Tag pushes and the default always ship
production. This is the hands-off path for the M3-4 Game Center
identity-verification device check: install from TestFlight, sign into Game
Center, host, and watch `wrangler tail newtonia-signal-beta` for
`game center verified idKind=… hash=…`. (A local Xcode run can do the same via
the scheme env var `NEWTONIA_SIGNAL_URL=wss://newtonia-signal-beta.gfmcc.workers.dev/ws`
— no rebuild needed.)

## Game Center in the simulator build

The simulator artifact compiles the Game Center achievements backend
(`GAME_CENTER_BUILD`), so on launch it may show a Game Center sign-in
sheet if the simulator has no signed-in account — cancel it once and the
game plays normally (earns are journaled locally and delivered whenever
an account signs in). Sandbox achievement testing needs a device build;
see ACHIEVEMENTS.md §2.
