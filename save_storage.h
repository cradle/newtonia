#ifndef SAVE_STORAGE_H
#define SAVE_STORAGE_H

#include <string>

// Resolves the on-disk path for a persisted file, abstracting *where* a given
// category of data lives so the Xbox GDK cloud-roaming backend can drop in
// behind the same calls without touching the save/load code itself
// (PORT_PLAN work-item #10; Xbox Play Anywhere, Phase 7).
//
// Two categories, mirroring how Newtonia already splits persisted data for
// Steam Auto-Cloud (steam/CLOUD.md):
//   - Roaming: progress that should follow the player across machines/devices
//     (savegame.dat, highscore.dat).
//   - Local:   machine-specific settings that must NOT roam (preferences.ini —
//     display/window size and key bindings are per-system).
//
// On every shipping platform today both categories resolve to the same
// SDL_GetPrefPath() directory; roaming is handled out-of-process (Steam
// Auto-Cloud on desktop, nothing on mobile/web). On Xbox GDK the Roaming
// category instead resolves to the XGameSaveFiles folder keyed to the
// signed-in XUser (XR-052), which the platform roams automatically — see the
// NEWTONIA_XGAMESAVE block in save_storage.cpp. The seam is intentionally
// path-only: XGameSaveFiles hands back a real folder you fopen() into, so the
// streaming I/O in savegame.cpp / highscore.cpp / preferences.cpp is untouched.

namespace SaveStorage {

enum class Category {
    Roaming,  // savegame.dat, highscore.dat — follows the player across devices
    Local,    // preferences.ini — machine-specific, never roams
};

// Absolute path to `filename` within `cat`. Returns "" when the directory
// can't be resolved; callers treat that as "no save available", exactly as the
// raw SDL_GetPrefPath() failure path did before this seam existed.
std::string path_for(Category cat, const char *filename);

} // namespace SaveStorage

#endif // SAVE_STORAGE_H
