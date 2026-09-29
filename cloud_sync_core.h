#pragma once
// The sync driver both cloud backends share (cloud_sync.h): it reads and
// writes the local files and applies cloud_sync_merge.h's rules against a
// backend's Store. The backends only move bytes to and from their cloud —
// iOS the iCloud key-value store (ios_cloud_sync.mm), Android a Play Games
// saved game (android_cloud_sync.cpp) — and keep the savegame stamp
// wherever suits the platform. Unit-tested with a fake Store by
// test/unit/cloud_sync_core_test.cpp.
#include <cstdint>
#include <string>

namespace CloudCore {

// One backend's cloud copy. f is a CloudSync::File.
struct Store {
  virtual ~Store() {}
  // The cloud's value for f; false when it holds none.
  virtual bool read(int f, std::string &out) = 0;
  virtual void write(int f, const std::string &bytes) = 0;
  // When this device last wrote or deleted its save; false = never recorded
  // (an install that predates sync — the file's mtime stands in).
  virtual bool load_save_stamp(uint64_t &out) = 0;
  virtual void store_save_stamp(uint64_t stamp) = 0;
};

// The pref-path file behind f. Tests point the files at a scratch
// directory (with a trailing slash); empty restores SDL's pref path.
std::string local_path(int f);
void set_local_dir_for_test(const std::string &dir);

uint64_t now_ms();

// Each merge returns true when it changed the LOCAL file.
bool sync_save(Store &s);
bool sync_stats(Store &s, bool may_write_local);
bool sync_highscore(Store &s);

// All three; a bitmask of the local files changed (1 << CloudSync::File).
// The caller flushes Stats before and reloads it after a STATS change.
unsigned sync_files(Store &s);

// After this device wrote or deleted its save: stamp it now and push.
void save_written(Store &s);

}  // namespace CloudCore
