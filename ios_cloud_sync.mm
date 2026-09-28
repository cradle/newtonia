// iCloud backend for CloudSync (cloud_sync.h): savegame.dat, stats.dat and
// highscore.dat roam through NSUbiquitousKeyValueStore. The store holds up
// to 1 MB in total (1024 keys); these three files are a few KB together
// (the save grows to tens of KB on a crowded late level), so one key each.
//
// Needs the com.apple.developer.ubiquity-kvstore-identifier entitlement
// (Entitlements*.plist) and the App ID's iCloud capability with a
// regenerated provisioning profile. Without an iCloud account the store
// still works, locally only, so nothing here needs to know.
//
// All file work runs on the game thread: the change notification only
// raises a flag, and CloudSync::poll() (called each frame from
// ios_main.mm) does the merge between frames, so it can never race a
// writer.
#if defined(__IOS__)

#import <Foundation/Foundation.h>
#include <SDL.h>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <string>

#include "atomic_file.h"
#include "cloud_sync.h"
#include "cloud_sync_merge.h"
#include "savegame.h"
#include "stats.h"

namespace {

const char *kFileNames[3] = {"savegame.dat", "stats.dat", "highscore.dat"};
NSString *const kKeys[3] = {@"savegame", @"stats", @"highscore"};
NSString *const kSaveStampDefault = @"cloud_sync.savegame_stamp";
// Far under the store's 1 MB per-key and total limits; a save this big
// would be damage, and pushing it would evict the other two keys.
const size_t kMaxValueBytes = 768 * 1024;

std::atomic<bool> g_pending(false);
bool g_started = false;
uint32_t g_generation = 0;
id g_observer = nil;

NSUbiquitousKeyValueStore *store() {
  return [NSUbiquitousKeyValueStore defaultStore];
}

std::string local_path(int f) {
  char *dir = SDL_GetPrefPath("cc.gfm", "newtonia");
  if (!dir) return "";
  std::string p = std::string(dir) + kFileNames[f];
  SDL_free(dir);
  return p;
}

bool read_local(int f, std::string &out) {
  out.clear();
  std::string path = local_path(f);
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
  std::string path = local_path(f);
  if (path.empty()) return false;
  return AtomicFile::write(path, [&](FILE *fp) {
    return bytes.empty() || fwrite(bytes.data(), bytes.size(), 1, fp) == 1;
  }, "cloud-sync");
}

bool read_cloud(int f, std::string &out) {
  out.clear();
  NSData *d = [store() dataForKey:kKeys[f]];
  if (!d) return false;
  out.assign((const char *)d.bytes, d.length);
  return true;
}

void write_cloud(int f, const std::string &bytes) {
  if (bytes.size() > kMaxValueBytes) {
    SDL_Log("cloud-sync: %s is %u bytes, not pushed", kFileNames[f],
            (unsigned)bytes.size());
    return;
  }
  [store() setData:[NSData dataWithBytes:bytes.data() length:bytes.size()]
            forKey:kKeys[f]];
}

uint64_t now_ms() {
  return (uint64_t)([[NSDate date] timeIntervalSince1970] * 1000.0);
}

// When this device last wrote or deleted its save. Before the first
// tracked write (an install that predates sync) the file's own mtime
// stands in, so an existing run competes fairly with the cloud's.
uint64_t local_save_stamp() {
  NSNumber *n = [[NSUserDefaults standardUserDefaults]
      objectForKey:kSaveStampDefault];
  if (n) return n.unsignedLongLongValue;
  NSString *p = [NSString stringWithUTF8String:local_path(CloudSync::SAVEGAME).c_str()];
  NSDictionary *attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:p
                                                                         error:nil];
  NSDate *m = attrs[NSFileModificationDate];
  return m ? (uint64_t)([m timeIntervalSince1970] * 1000.0) : 0;
}

void set_local_save_stamp(uint64_t s) {
  [[NSUserDefaults standardUserDefaults] setObject:@(s) forKey:kSaveStampDefault];
}

void push_save_record(uint64_t stamp) {
  CloudMerge::SaveRecord r;
  r.stamp = stamp;
  r.present = read_local(CloudSync::SAVEGAME, r.bytes);
  write_cloud(CloudSync::SAVEGAME, CloudMerge::save_record_bytes(r));
}

// ── merges: each returns true when it changed a LOCAL file ──────────────

bool sync_highscore() {
  std::string l, c;
  int32_t lv = 0, cv = 0;
  bool lok = read_local(CloudSync::HIGHSCORE, l) && CloudMerge::parse_highscore(l, lv);
  bool cok = read_cloud(CloudSync::HIGHSCORE, c) && CloudMerge::parse_highscore(c, cv);
  if (cok && (!lok || cv > lv)) {
    return write_local(CloudSync::HIGHSCORE, CloudMerge::highscore_bytes(cv));
  }
  if (lok && (!cok || lv > cv)) write_cloud(CloudSync::HIGHSCORE, CloudMerge::highscore_bytes(lv));
  return false;
}

bool sync_stats(bool may_write_local) {
  std::string l, c;
  std::vector<uint32_t> lf, cf;
  bool lok = read_local(CloudSync::STATS, l) && CloudMerge::parse_stats(l, lf);
  bool cok = read_cloud(CloudSync::STATS, c) && CloudMerge::parse_stats(c, cf);
  if (!lok && !cok) return false;
  std::vector<uint32_t> m = CloudMerge::merge_stats(lf, cf);
  if (!cok || m != cf) write_cloud(CloudSync::STATS, CloudMerge::stats_bytes(m));
  if (!may_write_local || (lok && m == lf)) return false;
  if (!write_local(CloudSync::STATS, CloudMerge::stats_bytes(m))) return false;
  Stats::reload();
  return true;
}

bool sync_save() {
  std::string c;
  CloudMerge::SaveRecord r;
  bool cok = read_cloud(CloudSync::SAVEGAME, c) && CloudMerge::parse_save_record(c, r);
  if (cok && r.present) {
    // Only ever land something that at least looks like a save; the full
    // semantic check still runs when CONTINUE loads it (net_state_sane).
    uint32_t magic = 0;
    std::memcpy(&magic, r.bytes.data(), 4);
    if (magic != Save::GameState::MAGIC) cok = false;
  }
  uint64_t ls = local_save_stamp();
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
    if (ok) set_local_save_stamp(r.stamp);
    return ok;
  }
  case CloudMerge::SAVE_PUSH:
    set_local_save_stamp(ls);
    push_save_record(ls);
    return false;
  case CloudMerge::SAVE_NONE:
    return false;
  }
  return false;
}

bool sync_all() {
  // Pending in-memory counters go to disk first, so the merge sees them.
  Stats::flush();
  bool changed = false;
  changed |= sync_save();
  changed |= sync_stats(true);
  changed |= sync_highscore();
  if (changed) g_generation++;
  return changed;
}

}  // namespace

namespace CloudSync {

void init() {
  if (g_started) return;
  g_started = true;
  g_observer = [[NSNotificationCenter defaultCenter]
      addObserverForName:NSUbiquitousKeyValueStoreDidChangeExternallyNotification
                  object:store()
                   queue:nil
              usingBlock:^(NSNotification *note) {
                NSNumber *reason =
                    note.userInfo[NSUbiquitousKeyValueStoreChangeReasonKey];
                if (reason && reason.integerValue ==
                                  NSUbiquitousKeyValueStoreQuotaViolationChange)
                  SDL_Log("cloud-sync: iCloud key-value quota exceeded");
                g_pending = true;
              }];
  [store() synchronize];
  sync_all();
}

bool poll() {
  if (!g_started || !g_pending.exchange(false)) return false;
  return sync_all();
}

uint32_t local_generation() { return g_generation; }

void local_written(File f) {
  if (!g_started) return;
  switch (f) {
  case SAVEGAME: {
    uint64_t s = now_ms();
    set_local_save_stamp(s);
    push_save_record(s);
    break;
  }
  case STATS:
    // Push only: the local file was just written from memory, and the
    // pull side (which may rewrite it) belongs to poll().
    sync_stats(false);
    break;
  case HIGHSCORE:
    sync_highscore();
    break;
  }
}

void app_background() {
  if (g_started) [store() synchronize];
}

void app_foreground() {
  if (!g_started) return;
  [store() synchronize];
  g_pending = true;
}

}  // namespace CloudSync

#endif  // __IOS__
