#include "save_storage.h"
#include <SDL.h>

// Organisation / app names passed to SDL_GetPrefPath(). These match the values
// that savegame.cpp, highscore.cpp and preferences.cpp each used inline before
// they were funnelled through this seam, so existing on-disk save locations are
// unchanged.
namespace {
const char *kOrg = "cc.gfm";
const char *kApp = "newtonia";

// The default backend on every platform except the Xbox GDK roaming path:
// SDL's per-user preference directory.
std::string sdl_pref_path(const char *filename) {
    char *dir = SDL_GetPrefPath(kOrg, kApp);
    if (!dir) return "";
    std::string path = std::string(dir) + filename;
    SDL_free(dir);
    return path;
}
} // namespace

#ifdef NEWTONIA_XGAMESAVE
// ─── Xbox GDK cloud-roaming backend (GDKX-gated — UNCOMPILED in CI) ───────────
//
// XGameSaveFiles gives a folder, keyed to an XUser, that the platform roams
// across devices (Xbox Play Anywhere, XR-052). Files written into it sync
// automatically, so all this layer needs is the folder path — the rest of the
// save code is unchanged. This block is GDKX/NDA-gated: it pulls in GDK headers
// and depends on the signed-in user from the (still-TODO) sign-in work
// (PORT_PLAN work-item #9), so it stays behind NEWTONIA_XGAMESAVE, which is
// defined nowhere until GDKX is installed. Inventory: GDKX_SYMBOLS.md §4.
//
// TODO(GDKX): wire this up once GDKX + sign-in (#9) exist. Sketch:
//   1. After XUserAddAsync resolves, call XGameSaveFilesGetFolderWithUiAsync
//      with the XUserHandle, drive the XAsyncBlock to completion on the app
//      task queue, and read the folder via XGameSaveFilesGetFolderWithUiResult.
//   2. Cache that folder string here; the platform keeps its contents roamed.
//   3. Verify every signature against the real <XGameSaveFiles.h> and flip the
//      GDKX_SYMBOLS.md §4 rows to ✅.
// #include <XGameSaveFiles.h>
namespace {
// Returns the cached XGameSaveFiles folder, or "" until sign-in resolves it.
std::string xgamesave_folder() {
    return "";  // TODO(GDKX): populated after XGameSaveFilesGetFolderWithUiAsync
}
} // namespace
#endif // NEWTONIA_XGAMESAVE

namespace SaveStorage {

std::string path_for(Category cat, const char *filename) {
#ifdef NEWTONIA_XGAMESAVE
    if (cat == Category::Roaming) {
        std::string folder = xgamesave_folder();
        if (!folder.empty()) return folder + filename;
        // Before sign-in resolves the roaming folder, fall back to the local
        // pref path so an early save still lands somewhere (it is reconciled to
        // the roaming store on the next save once the folder is available).
    }
#else
    (void)cat;  // off-console both categories share the SDL pref directory
#endif
    return sdl_pref_path(filename);
}

} // namespace SaveStorage
