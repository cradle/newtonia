#include "launch_shortcut.h"

#include <SDL.h>
#include <cstring>
#include <mutex>

namespace {

std::mutex s_mutex;
LaunchShortcut::Kind s_kind = LaunchShortcut::NONE;
Uint32 s_noted_at = 0;

// Long enough for a cold launch to reach its first menu tick (library
// load, prefs, the cloud merge), short enough that a shortcut tapped
// mid-game is gone by the time that game ends.
const Uint32 MAX_AGE_MS = 15000;

}  // namespace

namespace LaunchShortcut {

void note(const char *id) {
  if (!id) return;
  Kind k = NONE;
  if (!strcmp(id, "continue")) k = CONTINUE;
  else if (!strcmp(id, "new_game")) k = NEW_GAME;
  else if (!strcmp(id, "online")) k = ONLINE;
  if (k == NONE) return;
  std::lock_guard<std::mutex> lock(s_mutex);
  s_kind = k;
  s_noted_at = SDL_GetTicks();
  SDL_Log("launch shortcut: %s", id);
}

Kind take() {
  // Test hook, every build: NEWTONIA_LAUNCH_SHORTCUT=<id> acts as if that
  // shortcut launched the game (headless drivers; on Android it rides the
  // debug-build intent-extras bridge).
  static bool env_checked = false;
  if (!env_checked) {
    env_checked = true;
    if (const char *id = SDL_getenv("NEWTONIA_LAUNCH_SHORTCUT")) note(id);
  }
  std::lock_guard<std::mutex> lock(s_mutex);
  Kind k = s_kind;
  s_kind = NONE;
  if (k != NONE && SDL_GetTicks() - s_noted_at > MAX_AGE_MS) {
    SDL_Log("launch shortcut: too old, ignored");
    return NONE;
  }
  return k;
}

}  // namespace LaunchShortcut
