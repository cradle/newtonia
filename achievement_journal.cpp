#include "achievement_journal.h"
#include <SDL.h>
#include <cstdio>
#include <cstdint>

// pending_achievements.dat lives in the SDL pref path beside stats.dat and
// follows the same conventions (see stats.cpp): lazy load, version-gated
// reads so future fields can be appended safely, and batched writes —
// unlock records (pct 100) write through because the journal is their only
// safety net (ACHIEVEMENTS.md §2), while progress-only advances, which are
// re-derivable from the persisted game counters, batch until flush().
// No Emscripten syncfs call here: the web build compiles this file but has
// no backend that records to it.
//
// Layout (v1, redefined before any build shipped it): magic, version,
// last_owner (u8 len + bytes — the device's last-signed-in account, which
// unowned earns belong to), entry count, then per entry: u8 id length, id
// bytes, u8 pct, u8 owner length, owner bytes.

static const char *AJ_ORG  = "cc.gfm";
static const char *AJ_APP  = "newtonia";
static const char *AJ_FILE = "pending_achievements.dat";

static const uint32_t AJ_MAGIC   = 0x4E575041;  // "NWPA"
static const uint16_t AJ_VERSION = 1;

namespace {

bool loaded = false;
bool dirty = false;
std::string last_owner;
std::vector<AchievementJournal::Entry> entries;

std::string journal_path() {
  static std::string path;  // stable for the process lifetime
  static bool resolved = false;
  if (!resolved) {
    resolved = true;
    char *dir = SDL_GetPrefPath(AJ_ORG, AJ_APP);
    if (dir) {
      path = std::string(dir) + AJ_FILE;
      SDL_free(dir);
    }
  }
  return path;
}

bool read_string(FILE *f, std::string &out) {
  uint8_t len = 0;
  char buf[256];
  if (fread(&len, sizeof(len), 1, f) != 1) return false;
  if (fread(buf, 1, len, f) != len) return false;
  out.assign(buf, len);
  return true;
}

void write_string(FILE *f, const std::string &s) {
  uint8_t len = (uint8_t)(s.size() > 255 ? 255 : s.size());
  fwrite(&len, sizeof(len), 1, f);
  fwrite(s.data(), 1, len, f);
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
      read_string(f, last_owner) &&
      fread(&count, sizeof(count), 1, f) == 1) {
    for (uint32_t i = 0; i < count; i++) {
      AchievementJournal::Entry e;
      uint8_t pct = 0;
      if (!read_string(f, e.id) ||
          fread(&pct, sizeof(pct), 1, f) != 1 ||
          !read_string(f, e.owner))
        break;
      if (e.id.empty() || pct < 1) continue;
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
  write_string(f, last_owner);
  fwrite(&count, sizeof(count), 1, f);
  for (const AchievementJournal::Entry &e : entries) {
    write_string(f, e.id);
    uint8_t pct = (uint8_t)e.pct;
    fwrite(&pct, sizeof(pct), 1, f);
    write_string(f, e.owner);
  }
  fclose(f);
  dirty = false;
}

} // namespace

namespace AchievementJournal {

bool record(const char *id, int pct, const char *owner) {
  load();
  if (pct > 100) pct = 100;
  if (pct < 1 || !id || !*id) return false;
  if (!owner) owner = "";
  for (Entry &e : entries) {
    if (e.id != id || e.owner != owner) continue;
    if (pct <= e.pct) return false;
    e.pct = pct;
    if (pct >= 100) save(); else dirty = true;
    return true;
  }
  Entry e;
  e.id = id;
  e.pct = pct;
  e.owner = owner;
  entries.push_back(e);
  if (pct >= 100) save(); else dirty = true;
  return true;
}

void player_signed_in(const char *player) {
  load();
  if (!player || !*player) return;
  // Unowned earns belong to the device's previous account (offline play
  // never changes the configured account); with no previous account, this
  // first sign-in adopts them.
  const std::string &heir = last_owner.empty() ? player : last_owner;
  bool changed = false;
  for (Entry &e : entries) {
    if (!e.owner.empty()) continue;
    e.owner = heir;
    changed = true;
  }
  if (last_owner != player) {
    last_owner = player;
    changed = true;
  }
  if (changed) save();
}

std::vector<Entry> pending(const char *owner) {
  load();
  if (!owner) owner = "";
  std::vector<Entry> out;
  for (const Entry &e : entries)
    if (e.owner == owner) out.push_back(e);
  return out;
}

void confirm(const char *id, int pct, const char *owner) {
  load();
  if (!owner) owner = "";
  for (size_t i = 0; i < entries.size(); i++) {
    if (entries[i].id != id || entries[i].owner != owner) continue;
    if (entries[i].pct > pct) return;  // advanced since the send snapshot
    entries.erase(entries.begin() + i);
    save();
    return;
  }
}

void flush() {
  load();
  if (dirty) save();
}

void clear() {
  load();
  if (entries.empty()) return;
  entries.clear();
  save();
}

} // namespace AchievementJournal
