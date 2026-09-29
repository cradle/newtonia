#include "cloud_sync.h"

// No-op backend for every build without a cloud store. iOS supplies the
// real one in ios_cloud_sync.mm and Android in android_cloud_sync.cpp (both
// builds compile this file too, hence the guard).
#if !defined(__IOS__) && !(defined(__ANDROID__) && defined(PLAY_GAMES_BUILD))

namespace CloudSync {
void init() {}
bool poll() { return false; }
uint32_t local_generation() { return 0; }
void local_written(File) {}
void app_background() {}
void app_foreground() {}
}  // namespace CloudSync

#endif
