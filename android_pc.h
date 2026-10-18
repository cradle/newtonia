#pragma once

// Google Play Games on PC (the Windows client that runs Android games).
// NewtoniaActivity.exportPcMode() sets NEWTONIA_PC_MODE=1 before the native
// side starts when the system reports the HPE_EXPERIENCE feature — Google's
// documented runtime check for that client. In PC mode the Android build
// plays like the desktop one: keyboard and mouse, no touch OSD, and a mouse
// click is a menu tap (android_main.cpp). Phones, tablets, ChromeOS and cars
// never see the variable, so they keep the touch build exactly as before.
// A debug APK can force either way: adb shell am start -S
// -n org.newtonia/.NewtoniaActivity --es NEWTONIA_PC_MODE 1 (or 0).

#if defined(__ANDROID__)
#include <SDL.h>

inline bool android_pc_mode() {
  static const bool pc = [] {
    const char *v = SDL_getenv("NEWTONIA_PC_MODE");
    return v != nullptr && v[0] == '1';
  }();
  return pc;
}
#endif
