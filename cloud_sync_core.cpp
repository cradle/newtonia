#include "cloud_sync_core.h"

#include <SDL.h>
#include <sys/stat.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

#include "atomic_file.h"
#include "cloud_sync.h"
#include "cloud_sync_merge.h"
#include "savegame.h"

namespace {

const char *kFileNames[3] = {"savegame.dat", "stats.dat", "highscore.dat"};

// Far under both stores' per-value limits (iCloud 1 MB per key, a Play
// Games saved game 3 MB); a save this big would be damage, and pushing it
// would crowd out the other two.
const size_t kMaxValueBytes = 768 * 1024;

std::string g_test_dir;

bool read_local(int f, std::string &out) {
  out.clear();
  std::string path = CloudCore::local_path(f);
  if (path.empty()) return false;
  FILE *fp = fopen(path.c_str(), "rb");
  if (!fp) return false;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) out.append(buf, n);
  fclose(fp);
  return true;
}

bool write_local(int f, const std::string &bytes) {
  std::string path = CloudCore::local_path(f);
  if (path.empty()) return false;
  return AtomicFile::write(path, [&](FILE *fp) {
    return bytes.empty() || fwrite(bytes.data(), bytes.size(), 1, fp) == 1;
  }, "cloud-sync");
}

void write_cloud(CloudCore::Store &s, int f, const std::string &bytes) {
  if (bytes.size() > kMaxValueBytes) {
    SDL_Log("cloud-sync: %s is %u bytes, not pushed", kFileNames[f],
            (unsigned)bytes.size());
    return;
  }
  s.write(f, bytes);
}

uint64_t local_save_stamp(CloudCore::Store &s) {
  uint64_t stamp = 0;
  if (s.load_save_stamp(stamp)) return stamp;
  struct stat st;
  std::string path = CloudCore::local_path(CloudSync::SAVEGAME);
  if (path.empty() || stat(path.c_str(), &st) != 0) return 0;
  return (uint64_t)st.st_mtime * 1000;
}

void push_save_record(CloudCore::Store &s, uint64_t stamp) {
  CloudMerge::SaveRecord r;
  r.stamp = stamp;
  r.present = read_local(CloudSync::SAVEGAME, r.bytes);
  write_cloud(s, CloudSync::SAVEGAME, CloudMerge::save_record_bytes(r));
}

}  // namespace

namespace CloudCore {

std::string local_path(int f) {
  if (!g_test_dir.empty()) return g_test_dir + kFileNames[f];
  char *dir = SDL_GetPrefPath("cc.gfm", "newtonia");
  if (!dir) return "";
  std::string p = std::string(dir) + kFileNames[f];
  SDL_free(dir);
  return p;
}

void set_local_dir_for_test(const std::string &dir) { g_test_dir = dir; }

uint64_t now_ms() {
  return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

bool sync_highscore(Store &s) {
  std::string l, c;
  int32_t lv = 0, cv = 0;
  bool lok = read_local(CloudSync::HIGHSCORE, l) && CloudMerge::parse_highscore(l, lv);
  bool cok = s.read(CloudSync::HIGHSCORE, c) && CloudMerge::parse_highscore(c, cv);
  if (cok && (!lok || cv > lv))
    return write_local(CloudSync::HIGHSCORE, CloudMerge::highscore_bytes(cv));
  if (lok && (!cok || lv > cv))
    write_cloud(s, CloudSync::HIGHSCORE, CloudMerge::highscore_bytes(lv));
  return false;
}

bool sync_stats(Store &s, bool may_write_local) {
  std::string l, c;
  std::vector<uint32_t> lf, cf;
  bool lok = read_local(CloudSync::STATS, l) && CloudMerge::parse_stats(l, lf);
  bool cok = s.read(CloudSync::STATS, c) && CloudMerge::parse_stats(c, cf);
  if (!lok && !cok) return false;
  std::vector<uint32_t> m = CloudMerge::merge_stats(lf, cf);
  if (!cok || m != cf) write_cloud(s, CloudSync::STATS, CloudMerge::stats_bytes(m));
  if (!may_write_local || (lok && m == lf)) return false;
  return write_local(CloudSync::STATS, CloudMerge::stats_bytes(m));
}

bool sync_save(Store &s) {
  std::string c;
  CloudMerge::SaveRecord r;
  bool cok = s.read(CloudSync::SAVEGAME, c) && CloudMerge::parse_save_record(c, r);
  if (cok && r.present) {
    // Only ever land something that at least looks like a save; the full
    // semantic check still runs when CONTINUE loads it (net_state_sane).
    uint32_t magic = 0;
    std::memcpy(&magic, r.bytes.data(), 4);
    if (magic != Save::GameState::MAGIC) cok = false;
  }
  uint64_t ls = local_save_stamp(s);
  switch (CloudMerge::decide_save(ls, cok, r.stamp)) {
  case CloudMerge::SAVE_PULL: {
    bool ok;
    if (r.present) {
      ok = write_local(CloudSync::SAVEGAME, r.bytes);
    } else {
      // Only a removal that happened (or a file already gone) counts as
      // applying the tombstone. Any other failure keeps the old stamp, so
      // the next sync (foreground, or another device's push) retries
      // instead of treating the ended run as settled.
      std::string path = local_path(CloudSync::SAVEGAME);
      errno = 0;
      ok = !path.empty() &&
           (std::remove(path.c_str()) == 0 || errno == ENOENT);
      if (!ok)
        SDL_Log("cloud-sync: could not remove the ended save (errno %d)", errno);
    }
    if (ok) s.store_save_stamp(r.stamp);
    return ok;
  }
  case CloudMerge::SAVE_PUSH:
    s.store_save_stamp(ls);
    push_save_record(s, ls);
    return false;
  case CloudMerge::SAVE_NONE:
    return false;
  }
  return false;
}

unsigned sync_files(Store &s) {
  unsigned changed = 0;
  if (sync_save(s)) changed |= 1u << CloudSync::SAVEGAME;
  if (sync_stats(s, true)) changed |= 1u << CloudSync::STATS;
  if (sync_highscore(s)) changed |= 1u << CloudSync::HIGHSCORE;
  return changed;
}

void save_written(Store &s) {
  uint64_t stamp = now_ms();
  s.store_save_stamp(stamp);
  push_save_record(s, stamp);
}

}  // namespace CloudCore
