#include "achievements.h"

namespace Achievements {

// Platform backends (GDK in the private mirror, Steamworks, Play Games
// Services, Game Center — ACHIEVEMENTS.md §2) implement Backend::unlock/
// progress behind their own build flags and map the symbolic IDs to platform
// ones. Everything in this file, including cheat suppression, is shared and
// must not move into a backend.
#ifdef STEAM_BUILD
namespace Backend {  // steam_achievements.cpp
  void unlock(const char *id);
  void progress(const char *id, int pct);
}
#endif

namespace {

bool cheated_this_game = false;

void backend_unlock(const char *id) {
#ifdef STEAM_BUILD
  Backend::unlock(id);
#else
  (void)id;  // default backend: no-op
#endif
}

void backend_progress(const char *id, int pct) {
#ifdef STEAM_BUILD
  Backend::progress(id, pct);
#else
  (void)id; (void)pct;  // default backend: no-op
#endif
}

} // namespace

void unlock(const char *id) {
  if (cheated_this_game) return;
  backend_unlock(id);
}

void progress(const char *id, int pct) {
  if (cheated_this_game) return;
  if (pct > 100) pct = 100;
  if (pct < 1) return;
  backend_progress(id, pct);
  if (pct == 100)
    backend_unlock(id);  // 100% == unlock on every backend (§3 LCD semantics)
}

void note_cheat_used()    { cheated_this_game = true; }
void new_game_started()   { cheated_this_game = false; }
bool unlocks_suppressed() { return cheated_this_game; }

} // namespace Achievements
