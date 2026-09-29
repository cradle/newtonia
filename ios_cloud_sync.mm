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
#include <string>

#include "cloud_sync.h"
#include "cloud_sync_core.h"
#include "stats.h"

namespace {

NSString *const kKeys[3] = {@"savegame", @"stats", @"highscore"};
NSString *const kSaveStampDefault = @"cloud_sync.savegame_stamp";

std::atomic<bool> g_pending(false);
bool g_started = false;
uint32_t g_generation = 0;
id g_observer = nil;

NSUbiquitousKeyValueStore *store() {
  return [NSUbiquitousKeyValueStore defaultStore];
}

// One key per file; the savegame stamp in NSUserDefaults, which (like the
// app's files) goes with the app on a delete.
struct KvsStore : CloudCore::Store {
  bool read(int f, std::string &out) override {
    out.clear();
    NSData *d = [store() dataForKey:kKeys[f]];
    if (!d) return false;
    out.assign((const char *)d.bytes, d.length);
    return true;
  }
  void write(int f, const std::string &bytes) override {
    [store() setData:[NSData dataWithBytes:bytes.data() length:bytes.size()]
              forKey:kKeys[f]];
  }
  bool load_save_stamp(uint64_t &out) override {
    NSNumber *n = [[NSUserDefaults standardUserDefaults]
        objectForKey:kSaveStampDefault];
    if (!n) return false;
    out = n.unsignedLongLongValue;
    return true;
  }
  void store_save_stamp(uint64_t s) override {
    [[NSUserDefaults standardUserDefaults] setObject:@(s)
                                              forKey:kSaveStampDefault];
  }
};

KvsStore g_store;

bool sync_all() {
  // Pending in-memory counters go to disk first, so the merge sees them.
  Stats::flush();
  unsigned changed = CloudCore::sync_files(g_store);
  if (changed & (1u << CloudSync::STATS)) Stats::reload();
  if (changed) g_generation++;
  return changed != 0;
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
  case SAVEGAME:
    CloudCore::save_written(g_store);
    break;
  case STATS:
    // Push only: the local file was just written from memory, and the
    // pull side (which may rewrite it) belongs to poll().
    CloudCore::sync_stats(g_store, false);
    break;
  case HIGHSCORE:
    CloudCore::sync_highscore(g_store);
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
