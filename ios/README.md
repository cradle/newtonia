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
