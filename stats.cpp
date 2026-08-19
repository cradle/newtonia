#include "web_fs.h"
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
static const uint16_t ST_VERSION = 3;  // v2 appends shots_fired; v3 the
                                       // ship-kills..novas block (stats.h)

// Batch kill increments so heavy combat doesn't hit the disk (and, on web,
// IndexedDB) on every kill. The save/game-over paths call flush(), so at most
// a few seconds of kills are ever at risk. Shots batch coarser: an automatic
// discharges ~10/s, so the kill cadence would write every second.
static const uint32_t KILLS_PER_WRITE = 10;
static const uint32_t SHOTS_PER_WRITE = 50;

namespace {

bool     loaded = false;
bool     dirty = false;
uint32_t unsaved_kills = 0;
uint32_t unsaved_shots = 0;
uint32_t play_ms_accum = 0;       // sub-second remainder, memory only
uint32_t unsaved_play_seconds = 0;

uint32_t kills = 0;
uint32_t special_mask = 0;
uint32_t shots = 0;
uint32_t shipk = 0;
uint32_t death_count = 0;
uint32_t games = 0;
uint32_t best_level = 0;
uint32_t play_secs = 0;
uint32_t secondaries = 0;
uint32_t novas = 0;

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
    // v2: shots_fired. A v1 file ends here and the default 0 stands.
    if (version >= 2 && fread(&v, sizeof(v), 1, f) == 1) shots = v;
    // v3: the ship-kills..novas block, in write order below.
    if (version >= 3) {
      if (fread(&v, sizeof(v), 1, f) == 1) shipk = v;
      if (fread(&v, sizeof(v), 1, f) == 1) death_count = v;
      if (fread(&v, sizeof(v), 1, f) == 1) games = v;
      if (fread(&v, sizeof(v), 1, f) == 1) best_level = v;
      if (fread(&v, sizeof(v), 1, f) == 1) play_secs = v;
      if (fread(&v, sizeof(v), 1, f) == 1) secondaries = v;
      if (fread(&v, sizeof(v), 1, f) == 1) novas = v;
    }
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
         && fwrite(&special_mask, sizeof(special_mask), 1, f) == 1
         && fwrite(&shots, sizeof(shots), 1, f) == 1
         && fwrite(&shipk, sizeof(shipk), 1, f) == 1
         && fwrite(&death_count, sizeof(death_count), 1, f) == 1
         && fwrite(&games, sizeof(games), 1, f) == 1
         && fwrite(&best_level, sizeof(best_level), 1, f) == 1
         && fwrite(&play_secs, sizeof(play_secs), 1, f) == 1
         && fwrite(&secondaries, sizeof(secondaries), 1, f) == 1
         && fwrite(&novas, sizeof(novas), 1, f) == 1;
  fclose(f);
  if (!ok) return;
  dirty = false;
  unsaved_kills = 0;
  unsaved_shots = 0;
  unsaved_play_seconds = 0;
  // Persist to IndexedDB so the stats survive a page refresh.
  web_fs_sync("stats");
}

} // namespace

namespace Stats {

uint32_t lifetime_kills()    { load(); return kills; }
uint32_t special_kill_mask() { load(); return special_mask; }
uint32_t shots_fired()       { load(); return shots; }
uint32_t ship_kills()        { load(); return shipk; }
uint32_t deaths()            { load(); return death_count; }
uint32_t games_played()      { load(); return games; }
uint32_t highest_level()     { load(); return best_level; }
uint32_t play_seconds()      { load(); return play_secs; }
uint32_t secondaries_used()  { load(); return secondaries; }
uint32_t novas_detonated()   { load(); return novas; }

void add_kill() {
  load();
  kills++;
  unsaved_kills++;
  dirty = true;
  if (unsaved_kills >= KILLS_PER_WRITE) save();
}

void add_shot() {
  load();
  shots++;
  unsaved_shots++;
  dirty = true;
  if (unsaved_shots >= SHOTS_PER_WRITE) save();
}

void note_special_kill(Special s) {
  load();
  uint32_t bit = 1u << (int)s;
  if (special_mask & bit) return;
  special_mask |= bit;
  save();  // first kill of a type is rare — write through immediately
}

// Bursty counters mark dirty and ride the next batched save or flush()
// (station waves land ship kills in clusters; secondaries follow combat).
void add_ship_kill()      { load(); shipk++;       dirty = true; }
void add_secondary_used() { load(); secondaries++; dirty = true; }

// Rare events write through, like note_special_kill.
void add_death()       { load(); death_count++; save(); }
void add_game_played() { load(); games++;       save(); }
void add_nova()        { load(); novas++;       save(); }

void note_level_reached(uint32_t displayed_level) {
  load();
  if (displayed_level <= best_level) return;
  best_level = displayed_level;
  save();  // a new personal best is rare — write through
}

void add_play_time(int ms) {
  if (ms <= 0) return;
  load();
  play_ms_accum += (uint32_t)ms;
  if (play_ms_accum < 1000) return;
  play_secs += play_ms_accum / 1000;
  unsaved_play_seconds += play_ms_accum / 1000;
  play_ms_accum %= 1000;
  dirty = true;
  // A quiet cruise can go minutes without a kill- or shot-triggered save;
  // don't let more than a minute of time ride on the next flush.
  if (unsaved_play_seconds >= 60) save();
}

void flush() {
  if (dirty) save();
}

} // namespace Stats
