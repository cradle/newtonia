#include "options.h"
#include <SDL.h>
#include <cstdio>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

Options g_options;

static const char* OPT_ORG  = "cc.gfm";
static const char* OPT_APP  = "newtonia";
static const char* OPT_FILE = "options.dat";

Options load_options() {
  Options opts;
  char *path = SDL_GetPrefPath(OPT_ORG, OPT_APP);
  if (!path) return opts;
  std::string filepath = std::string(path) + OPT_FILE;
  SDL_free(path);
  FILE *f = fopen(filepath.c_str(), "rb");
  if (f) {
    Options loaded;
    if (fread(&loaded, sizeof(Options), 1, f) == 1 &&
        loaded.keyboard_sensitivity >= 0.1f &&
        loaded.keyboard_sensitivity <= 5.0f) {
      opts = loaded;
    }
    fclose(f);
  }
  return opts;
}

void save_options(const Options &opts) {
  char *path = SDL_GetPrefPath(OPT_ORG, OPT_APP);
  if (!path) return;
  std::string filepath = std::string(path) + OPT_FILE;
  SDL_free(path);
  FILE *f = fopen(filepath.c_str(), "wb");
  if (f) {
    fwrite(&opts, sizeof(Options), 1, f);
    fclose(f);
#ifdef __EMSCRIPTEN__
    EM_ASM(
      FS.syncfs(false, function(err) {
        if (err) console.error('[newtonia] IDBFS options save failed:', err);
      });
    );
#endif
  }
}
