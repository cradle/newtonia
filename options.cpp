#include "options.h"
#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

Options g_options;

static const char* OPT_ORG  = "cc.gfm";
static const char* OPT_APP  = "newtonia";
static const char* OPT_FILE = "options.ini";

static std::string opt_path() {
  char *dir = SDL_GetPrefPath(OPT_ORG, OPT_APP);
  if (!dir) return "";
  std::string p = std::string(dir) + OPT_FILE;
  SDL_free(dir);
  return p;
}

Options load_options() {
  Options opts;  // default-initialized — all fields have built-in defaults

  std::string path = opt_path();
  if (path.empty()) return opts;

  FILE *f = fopen(path.c_str(), "r");
  if (!f) return opts;

  char line[256];
  while (fgets(line, sizeof(line), f)) {
    char *p = line;
    // skip leading whitespace
    while (*p == ' ' || *p == '\t') p++;
    // skip blank lines, comments, and section headers
    if (*p == '\0' || *p == '\r' || *p == '\n' || *p == '#' || *p == ';' || *p == '[')
      continue;

    // split on '='
    char *eq = strchr(p, '=');
    if (!eq) continue;

    // extract and trim key
    char key[64] = {};
    int klen = (int)(eq - p);
    if (klen <= 0 || klen >= (int)sizeof(key)) continue;
    memcpy(key, p, klen);
    while (klen > 0 && (key[klen-1] == ' ' || key[klen-1] == '\t')) klen--;
    key[klen] = '\0';

    // parse value as float (sscanf skips leading whitespace)
    float v;
    if (sscanf(eq + 1, " %f", &v) != 1) continue;

    if (strcmp(key, "keyboard_sensitivity") == 0) {
      if (v >= 0.1f && v <= 5.0f) opts.keyboard_sensitivity = v;
    }
  }
  fclose(f);
  return opts;
}

void save_options(const Options &opts) {
  std::string path = opt_path();
  if (path.empty()) return;

  FILE *f = fopen(path.c_str(), "w");
  if (!f) return;

  fprintf(f, "# Newtonia options\n");
  fprintf(f, "\n");
  fprintf(f, "[controls]\n");
  fprintf(f, "# keyboard rotation speed multiplier: 0.5 (slow) to 2.0 (fast)\n");
  fprintf(f, "keyboard_sensitivity=%.2f\n", opts.keyboard_sensitivity);

  fclose(f);

#ifdef __EMSCRIPTEN__
  EM_ASM(
    FS.syncfs(false, function(err) {
      if (err) console.error('[newtonia] IDBFS options save failed:', err);
    });
  );
#endif
}
