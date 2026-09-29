#pragma once
// Pure merge rules for CloudSync (cloud_sync.h) — header-inline, no
// platform calls, unit-tested by test/unit/cloud_sync_merge_test.cpp. The
// backends move bytes; every decision about WHICH bytes win lives here.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace CloudMerge {

// ── highscore.dat: one native int ────────────────────────────────────────
inline bool parse_highscore(const std::string &b, int32_t &out) {
  if (b.size() < sizeof(int32_t)) return false;
  std::memcpy(&out, b.data(), sizeof(int32_t));
  return true;
}
inline std::string highscore_bytes(int32_t v) {
  return std::string(reinterpret_cast<const char *>(&v), sizeof(v));
}

// ── stats.dat (stats.cpp): u32 magic "NWST", u16 disk version (always 1 —
// see ST_DISK_VERSION), then u32 counters read by fread success. Counter 1
// is the special-kill BIT MASK; every other counter only ever grows. ─────
static const uint32_t STATS_MAGIC = 0x4E575354;
static const uint16_t STATS_DISK_VERSION = 1;
static const size_t   STATS_MASK_FIELD = 1;
static const size_t   STATS_MAX_FIELDS = 64;  // a sanity cap, not a layout

inline bool parse_stats(const std::string &b, std::vector<uint32_t> &out) {
  out.clear();
  if (b.size() < 6) return false;
  uint32_t magic = 0;
  uint16_t version = 0;
  std::memcpy(&magic, b.data(), 4);
  std::memcpy(&version, b.data() + 4, 2);
  if (magic != STATS_MAGIC || version < 1) return false;
  size_t n = (b.size() - 6) / 4;
  if (n > STATS_MAX_FIELDS) n = STATS_MAX_FIELDS;
  out.resize(n);
  for (size_t i = 0; i < n; i++) std::memcpy(&out[i], b.data() + 6 + 4 * i, 4);
  return true;
}

inline std::string stats_bytes(const std::vector<uint32_t> &f) {
  std::string b(6 + 4 * f.size(), '\0');
  std::memcpy(&b[0], &STATS_MAGIC, 4);
  std::memcpy(&b[4], &STATS_DISK_VERSION, 2);
  for (size_t i = 0; i < f.size(); i++) std::memcpy(&b[6 + 4 * i], &f[i], 4);
  return b;
}

// Per counter the larger value, the mask OR'd; the longer file's extra
// counters (a newer build's appended fields) carry over unchanged.
inline std::vector<uint32_t> merge_stats(const std::vector<uint32_t> &a,
                                         const std::vector<uint32_t> &b) {
  std::vector<uint32_t> out(a.size() > b.size() ? a.size() : b.size(), 0);
  for (size_t i = 0; i < out.size(); i++) {
    uint32_t x = i < a.size() ? a[i] : 0;
    uint32_t y = i < b.size() ? b[i] : 0;
    out[i] = (i == STATS_MASK_FIELD) ? (x | y) : (x > y ? x : y);
  }
  return out;
}

// ── savegame.dat: newest write wins, deletes included. The cloud value is
// a small envelope: u64 stamp (ms since the epoch at the write), u8
// present (0 = the run ended/was deleted), then the file's bytes. ────────
struct SaveRecord {
  uint64_t stamp = 0;
  bool present = false;
  std::string bytes;
};

inline std::string save_record_bytes(const SaveRecord &r) {
  std::string b(9, '\0');
  std::memcpy(&b[0], &r.stamp, 8);
  b[8] = r.present ? 1 : 0;
  if (r.present) b += r.bytes;
  return b;
}

inline bool parse_save_record(const std::string &b, SaveRecord &r) {
  if (b.size() < 9) return false;
  std::memcpy(&r.stamp, b.data(), 8);
  r.present = b[8] != 0;
  r.bytes = r.present ? b.substr(9) : std::string();
  // A present record must at least carry a save header (magic + version);
  // anything shorter is damage, not a save.
  return !r.present || r.bytes.size() >= 6;
}

enum SaveAction { SAVE_NONE, SAVE_PULL, SAVE_PUSH };

// local_stamp: when this device last wrote or deleted its save (0 = never
// known). cloud_ok false = no usable cloud record yet.
inline SaveAction decide_save(uint64_t local_stamp, bool cloud_ok,
                              uint64_t cloud_stamp) {
  if (!cloud_ok) return local_stamp ? SAVE_PUSH : SAVE_NONE;
  if (cloud_stamp > local_stamp) return SAVE_PULL;
  if (local_stamp > cloud_stamp) return SAVE_PUSH;
  return SAVE_NONE;
}

// ── one-value stores (a Play Games saved game) carry all three files in a
// bundle: u32 magic "NWCB", u8 version 1, then per file (SAVEGAME, STATS,
// HIGHSCORE order) u8 present, u32 length, the bytes — each value exactly
// what iOS keeps under its own key. An empty blob is a brand-new saved
// game: all three absent. ──────────────────────────────────────────────
static const uint32_t BUNDLE_MAGIC = 0x4E574342;  // "NWCB"
static const uint8_t  BUNDLE_VERSION = 1;
static const int      BUNDLE_FILES = 3;

inline std::string bundle_bytes(const std::string v[BUNDLE_FILES],
                                const bool has[BUNDLE_FILES]) {
  std::string b(5, '\0');
  std::memcpy(&b[0], &BUNDLE_MAGIC, 4);
  b[4] = (char)BUNDLE_VERSION;
  for (int i = 0; i < BUNDLE_FILES; i++) {
    uint32_t len = has[i] ? (uint32_t)v[i].size() : 0;
    char hdr[5];
    hdr[0] = has[i] ? 1 : 0;
    std::memcpy(hdr + 1, &len, 4);
    b.append(hdr, 5);
    if (has[i]) b += v[i];
  }
  return b;
}

inline bool parse_bundle(const std::string &b, std::string v[BUNDLE_FILES],
                         bool has[BUNDLE_FILES]) {
  for (int i = 0; i < BUNDLE_FILES; i++) { v[i].clear(); has[i] = false; }
  if (b.empty()) return true;
  if (b.size() < 5) return false;
  uint32_t magic = 0;
  std::memcpy(&magic, b.data(), 4);
  // A newer version may append files; the first three keep their layout.
  if (magic != BUNDLE_MAGIC || (uint8_t)b[4] < BUNDLE_VERSION) return false;
  size_t at = 5;
  for (int i = 0; i < BUNDLE_FILES; i++) {
    if (b.size() - at < 5) return false;
    bool present = b[at] != 0;
    uint32_t len = 0;
    std::memcpy(&len, b.data() + at + 1, 4);
    at += 5;
    if (len > b.size() - at) return false;
    if (present) { v[i] = b.substr(at, len); has[i] = true; }
    at += len;
  }
  return true;
}

// Two copies of the bundle folded into one by the same rules the local
// merge uses: the higher score, each stats counter's max (mask OR'd), the
// save record with the newer stamp (a tie keeps a's). Order only matters on
// that tie. A copy that doesn't parse, or a value that doesn't, gives way to
// the other side. Used where a saved game is written or a conflict is
// resolved, so neither ever drops what the cloud already held.
inline std::string merge_bundles(const std::string &a, const std::string &b) {
  std::string va[BUNDLE_FILES], vb[BUNDLE_FILES], out[BUNDLE_FILES];
  bool ha[BUNDLE_FILES], hb[BUNDLE_FILES], ho[BUNDLE_FILES];
  if (!parse_bundle(a, va, ha)) parse_bundle(std::string(), va, ha);
  if (!parse_bundle(b, vb, hb)) parse_bundle(std::string(), vb, hb);
  for (int i = 0; i < BUNDLE_FILES; i++) { out[i] = va[i]; ho[i] = ha[i]; }

  // Savegame (file 0).
  {
    SaveRecord ra, rb;
    bool oka = ha[0] && parse_save_record(va[0], ra);
    bool okb = hb[0] && parse_save_record(vb[0], rb);
    if (okb && (!oka || rb.stamp > ra.stamp)) { out[0] = vb[0]; ho[0] = true; }
    else if (!oka) { out[0].clear(); ho[0] = false; }
  }
  // Stats (file 1).
  {
    std::vector<uint32_t> sa, sb;
    bool oka = ha[1] && parse_stats(va[1], sa);
    bool okb = hb[1] && parse_stats(vb[1], sb);
    if (oka && okb) { out[1] = stats_bytes(merge_stats(sa, sb)); ho[1] = true; }
    else if (okb) { out[1] = vb[1]; ho[1] = true; }
    else if (!oka) { out[1].clear(); ho[1] = false; }
  }
  // High score (file 2).
  {
    int32_t sa = 0, sb = 0;
    bool oka = ha[2] && parse_highscore(va[2], sa);
    bool okb = hb[2] && parse_highscore(vb[2], sb);
    if (oka && okb) { out[2] = highscore_bytes(sa > sb ? sa : sb); ho[2] = true; }
    else if (okb) { out[2] = vb[2]; ho[2] = true; }
    else if (!oka) { out[2].clear(); ho[2] = false; }
  }
  return bundle_bytes(out, ho);
}

}  // namespace CloudMerge
