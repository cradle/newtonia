#include "cloud_sync.h"

// No-op backend for every build without a cloud store. iOS supplies the
// real one in ios_cloud_sync.mm and Android in android_cloud_sync.cpp (both
// builds compile this file too, hence the guard).
#if !defined(__IOS__) && !(defined(__ANDROID__) && defined(PLAY_GAMES_BUILD))

#include <SDL.h>
#include <cstdlib>

namespace CloudSync {
void init() {}
// Test hook: NEWTONIA_TEST_CLOUD_HOLD_MS=N holds the menu's game rows for
// N ms after the first ask, as an Android launch waiting on Play Games
// would, so a desktop run can show it (screenshot, e2e).
bool first_sync_pending() {
  static int hold_ms = -1;
  static Uint32 start = 0;
  if (hold_ms < 0) {
    const char *v = SDL_getenv("NEWTONIA_TEST_CLOUD_HOLD_MS");
    hold_ms = v ? atoi(v) : 0;
    if (hold_ms < 0) hold_ms = 0;
    start = SDL_GetTicks();
  }
  return hold_ms > 0 && SDL_GetTicks() - start < (Uint32)hold_ms;
}
bool poll() { return false; }
uint32_t local_generation() { return 0; }
void local_written(File) {}
void app_background() {}
void app_foreground() {}
}  // namespace CloudSync

#endif
