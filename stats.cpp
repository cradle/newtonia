#include "stats.h"
#include <SDL.h>
#include <string>
#include <cstdio>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// stats.dat lives in the SDL pref path (the "Roaming" category) alongside
// highscore.dat and savegame.dat. Same forward-compatibility convention as
// savegame.dat: only ever APPEND fields, gated on version at read time, so a
// v1 file loads under any future version with the new fields defaulted.
// When a SaveStorage abstraction lands (xbox/PORT_PLAN.md), this module's
// file I/O moves behind it unchanged. On Steam this file likely needs adding
// to the depot's Auto-Cloud patterns so lifetime stats persist across
// installs (ACHIEVEMENTS.md §4).

static const char *ST_ORG  = "cc.gfm";
static const char *ST_APP  = "newtonia";
static const char *ST_FILE = "stats.dat";

static const uint32_t ST_MAGIC   = 0x4E575354;  // "NWST"
static const uint16_t ST_VERSION = 1;

// Batch kill increments so heavy combat doesn't hit the disk (and, on web,
// IndexedDB) on every kill. The save/game-over paths call flush(), so at most
// a few seconds of kills are ever at risk.
static const uint32_t KILLS_PER_WRITE = 10;

namespace {

bool     loaded = false;
bool     dirty = false;
uint32_t unsaved_kills = 0;

uint32_t kills = 0;
uint32_t special_mask = 0;

std::string stats_path() {
  char *dir = SDL_GetPrefPath(ST_ORG, ST_APP);
  if (!dir) return "";
  std::string path = std::string(dir) + ST_FILE;
  SDL_free(dir);
  return path;
}

void load() {
  if (loaded) return;
  loaded = true;
  std::string path = stats_path();
  if (path.empty()) return;
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) return;
  uint32_t magic = 0;
  uint16_t version = 0;
  if (fread(&magic, sizeof(magic), 1, f) == 1 && magic == ST_MAGIC &&
      fread(&version, sizeof(version), 1, f) == 1 &&
      version >= 1 && version <= ST_VERSION) {
    uint32_t v = 0;
    if (fread(&v, sizeof(v), 1, f) == 1) kills = v;
    if (fread(&v, sizeof(v), 1, f) == 1) special_mask = v;
  }
  fclose(f);
}

void save() {
  std::string path = stats_path();
  if (path.empty()) return;
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return;
  bool ok = fwrite(&ST_MAGIC, sizeof(ST_MAGIC), 1, f) == 1
         && fwrite(&ST_VERSION, sizeof(ST_VERSION), 1, f) == 1
         && fwrite(&kills, sizeof(kills), 1, f) == 1
         && fwrite(&special_mask, sizeof(special_mask), 1, f) == 1;
  fclose(f);
  if (!ok) return;
  dirty = false;
  unsaved_kills = 0;
#ifdef __EMSCRIPTEN__
  // Flush the in-memory filesystem to IndexedDB so the stats survive
  // page refreshes.
  EM_ASM(
    FS.syncfs(false, function(err) {
      if (err) console.error('[newtonia] IDBFS stats save failed:', err);
    });
  );
#endif
}

} // namespace

namespace Stats {

uint32_t lifetime_kills()    { load(); return kills; }
uint32_t special_kill_mask() { load(); return special_mask; }

void add_kill() {
  load();
  kills++;
  unsaved_kills++;
  dirty = true;
  if (unsaved_kills >= KILLS_PER_WRITE) save();
}

void note_special_kill(Special s) {
  load();
  uint32_t bit = 1u << (int)s;
  if (special_mask & bit) return;
  special_mask |= bit;
  save();  // first kill of a type is rare — write through immediately
}

void flush() {
  if (dirty) save();
}

} // namespace Stats
