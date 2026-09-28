#include "cloud_sync.h"

// No-op backend for every build without a cloud store. iOS supplies the
// real one in ios_cloud_sync.mm (the iOS project and ios.yml compile both
// files, hence the guard).
#if !defined(__IOS__)

namespace CloudSync {
void init() {}
bool poll() { return false; }
uint32_t local_generation() { return 0; }
void local_written(File) {}
void app_background() {}
void app_foreground() {}
}  // namespace CloudSync

#endif
