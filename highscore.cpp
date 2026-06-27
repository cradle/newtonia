#include "highscore.h"
#include "save_storage.h"
#include <string>
#include <cstdio>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static const char* HS_FILE = "highscore.dat";

// The high score is Roaming data — it follows the player across devices
// alongside the savegame (see save_storage.cpp).
static std::string highscore_path() {
  return SaveStorage::path_for(SaveStorage::Category::Roaming, HS_FILE);
}

int load_high_score() {
  std::string filepath = highscore_path();
  if (filepath.empty()) return 0;
  int score = 0;
  FILE *f = fopen(filepath.c_str(), "rb");
  if (f) { fread(&score, sizeof(int), 1, f); fclose(f); }
  return score;
}

void save_high_score(int score) {
  if (score <= load_high_score()) return;
  std::string filepath = highscore_path();
  if (filepath.empty()) return;
  FILE *f = fopen(filepath.c_str(), "wb");
  if (f) {
    fwrite(&score, sizeof(int), 1, f);
    fclose(f);
#ifdef __EMSCRIPTEN__
    // Flush the in-memory filesystem to IndexedDB so the score
    // survives page refreshes.
    EM_ASM(
      FS.syncfs(false, function(err) {
        if (err) console.error('[newtonia] IDBFS save failed:', err);
      });
    );
#endif
  }
}
