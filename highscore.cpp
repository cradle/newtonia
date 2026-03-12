#include "highscore.h"
#include <SDL.h>
#include <string>
#include <cstdio>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static const char* HS_ORG  = "org.newtonia";
static const char* HS_APP  = "newtonia";
static const char* HS_FILE = "highscore.dat";

int load_high_score() {
  char *path = SDL_GetPrefPath(HS_ORG, HS_APP);
  if (!path) return 0;
  std::string filepath = std::string(path) + HS_FILE;
  SDL_free(path);
  int score = 0;
  FILE *f = fopen(filepath.c_str(), "rb");
  if (f) { fread(&score, sizeof(int), 1, f); fclose(f); }
  return score;
}

void save_high_score(int score) {
  if (score <= load_high_score()) return;
  char *path = SDL_GetPrefPath(HS_ORG, HS_APP);
  if (!path) return;
  std::string filepath = std::string(path) + HS_FILE;
  SDL_free(path);
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
