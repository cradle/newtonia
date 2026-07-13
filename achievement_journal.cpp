#include "achievement_journal.h"
#include <SDL.h>
#include <cstdio>
#include <cstdint>

// pending_achievements.dat lives in the SDL pref path beside stats.dat and
// follows the same conventions (see stats.cpp): lazy load, rewrite-on-change
// (the journal is at most one entry per achievement, so writes are tiny and
// rare), and version-gated reads so future fields can be appended safely.
// No Emscripten syncfs call here: the web build compiles this file but has
// no backend that records to it.

static const char *AJ_ORG  = "cc.gfm";
static const char *AJ_APP  = "newtonia";
static const char *AJ_FILE = "pending_achievements.dat";

static const uint32_t AJ_MAGIC   = 0x4E575041;  // "NWPA"
static const uint16_t AJ_VERSION = 1;

namespace {

bool loaded = false;
std::vector<AchievementJournal::Entry> entries;

std::string journal_path() {
  char *dir = SDL_GetPrefPath(AJ_ORG, AJ_APP);
  if (!dir) return "";
  std::string path = std::string(dir) + AJ_FILE;
  SDL_free(dir);
  return path;
}

void load() {
  if (loaded) return;
  loaded = true;
  std::string path = journal_path();
  if (path.empty()) return;
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) return;
  uint32_t magic = 0;
  uint16_t version = 0;
  uint32_t count = 0;
  if (fread(&magic, sizeof(magic), 1, f) == 1 && magic == AJ_MAGIC &&
      fread(&version, sizeof(version), 1, f) == 1 &&
      version >= 1 && version <= AJ_VERSION &&
      fread(&count, sizeof(count), 1, f) == 1) {
    for (uint32_t i = 0; i < count; i++) {
      uint8_t len = 0, pct = 0;
      char id[256];
      if (fread(&len, sizeof(len), 1, f) != 1 ||
          fread(id, 1, len, f) != len ||
          fread(&pct, sizeof(pct), 1, f) != 1)
        break;
      if (len == 0 || pct < 1) continue;
      AchievementJournal::Entry e;
      e.id.assign(id, len);
      e.pct = pct > 100 ? 100 : pct;
      entries.push_back(e);
    }
  }
  fclose(f);
}

void save() {
  std::string path = journal_path();
  if (path.empty()) return;
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return;
  uint32_t count = (uint32_t)entries.size();
  fwrite(&AJ_MAGIC, sizeof(AJ_MAGIC), 1, f);
  fwrite(&AJ_VERSION, sizeof(AJ_VERSION), 1, f);
  fwrite(&count, sizeof(count), 1, f);
  for (const AchievementJournal::Entry &e : entries) {
    uint8_t len = (uint8_t)(e.id.size() > 255 ? 255 : e.id.size());
    uint8_t pct = (uint8_t)e.pct;
    fwrite(&len, sizeof(len), 1, f);
    fwrite(e.id.data(), 1, len, f);
    fwrite(&pct, sizeof(pct), 1, f);
  }
  fclose(f);
}

} // namespace

namespace AchievementJournal {

bool record(const char *id, int pct) {
  load();
  if (pct > 100) pct = 100;
  if (pct < 1 || !id || !*id) return false;
  for (Entry &e : entries) {
    if (e.id != id) continue;
    if (pct <= e.pct) return false;
    e.pct = pct;
    save();
    return true;
  }
  Entry e;
  e.id = id;
  e.pct = pct;
  entries.push_back(e);
  save();
  return true;
}

void confirm(const char *id, int pct) {
  load();
  for (size_t i = 0; i < entries.size(); i++) {
    if (entries[i].id != id) continue;
    if (entries[i].pct > pct) return;  // advanced since the send snapshot
    entries.erase(entries.begin() + i);
    save();
    return;
  }
}

std::vector<Entry> pending() {
  load();
  return entries;
}

} // namespace AchievementJournal
