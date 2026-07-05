# Newtonia – iOS Simulator

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
it hand-builds the source list and deps, not the xcodeproj). For a local
Xcode build with netplay:

1. `./build_netplay_deps_ios.sh device` (or `simulator`) — static MbedTLS +
   libdatachannel into `netplay-libs-ios[-sim]/`.
2. In the project's Build Settings: add `NEWTONIA_NET_RTC=1` to
   *Preprocessor Macros*, and `$(SRCROOT)/../netplay-libs-ios/include` to
   *Header Search Paths*.
3. Add every `.a` from `netplay-libs-ios/lib` to *Link Binary With
   Libraries*.

Without those steps the project still builds — the netplay TUs compile
empty and the menu simply hides ONLINE.
