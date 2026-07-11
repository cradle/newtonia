#include "achievements.h"

namespace Achievements {

namespace {

bool cheated_this_game = false;

// Default no-op backend. Platform backends (GDK in the private mirror,
// Steamworks, Play Games Services, Game Center — ACHIEVEMENTS.md §2) replace
// these two functions behind their own build flags and map the symbolic IDs
// to platform ones. Everything above them, including cheat suppression, is
// shared and must not move into a backend.
void backend_unlock(const char * /*id*/) {}
void backend_progress(const char * /*id*/, int /*pct*/) {}

} // namespace

void unlock(const char *id) {
  if (cheated_this_game) return;
  backend_unlock(id);
}

void progress(const char *id, int pct) {
  if (cheated_this_game) return;
  if (pct > 100) pct = 100;
  if (pct < 1) return;
  if (pct == 100) {
    backend_unlock(id);  // 100% == unlock on every backend (§3 LCD semantics)
  } else {
    backend_progress(id, pct);
  }
}

void note_cheat_used()    { cheated_this_game = true; }
void new_game_started()   { cheated_this_game = false; }
bool unlocks_suppressed() { return cheated_this_game; }

} // namespace Achievements
