// Game Center achievements backend (ACHIEVEMENTS.md §2) — compiled only when
// GAME_CENTER_BUILD is defined, which the iOS Xcode project and the ios.yml
// simulator CI build both set (each also links GameKit.framework). No other
// build compiles this file: the Makefile and CMake source lists take *.cpp
// plus explicitly named .mm files only, and the GAME_CENTER_BUILD guard
// keeps the body empty anywhere else.
//
// Shape of the backend:
// - GKAchievement.percentComplete is natively 0-100, so the seam's percent
//   maps 1:1 — event-only unlocks are simply a report of 100. GameKit draws
//   the unlock banner (showsCompletionBanner), and the server keeps the
//   highest percent it has ever seen, so re-sends never regress progress.
// - GameKit does NOT reliably queue reports made offline, so every earn is
//   written to the shared pending journal (achievement_journal.h) BEFORE a
//   delivery attempt, keyed by the authenticated player so an account
//   switch can never credit one player's earn to another. Entries are
//   confirmed out only after the server's achievement list shows the earn
//   (a nil reportAchievements error alone is not trusted — a batch can
//   "succeed" while the server silently drops an identifier that is not
//   defined in App Store Connect, and the journal is the only safety net
//   for event-only earns). Unconfirmed entries are resubmitted on sign-in
//   and app foreground.
// - Progress-only changes batch on a send interval (a report is a network
//   RPC, unlike Steam's in-memory SetStat); a NEW unlock flushes the whole
//   journal immediately so its banner is instant (an unlock that races an
//   in-flight send whose batch then fails waits for the next earn,
//   foreground, or interval — delivery is never lost, only that banner is
//   late). Re-fired unlocks the seam sends every generation rebuild dedupe
//   against the confirmed cache and the journal, so they never hit the
//   network.
// - Authentication is GameKit's: the authenticateHandler either hands us a
//   sign-in view controller to present over the SDL window or reports the
//   player signed in. Signed-out play earns nothing on Game Center until
//   the device's account signs back in — unowned journal entries resolve
//   to the device's last-signed-in account (achievement_journal.h), never
//   to a different account.

#ifdef GAME_CENTER_BUILD

#import <Foundation/Foundation.h>
#import <GameKit/GameKit.h>
#import <UIKit/UIKit.h>

#include "achievement_journal.h"

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

// Symbolic ID (ACHIEVEMENTS.md §5) → Game Center achievement identifier.
// The App Store Connect definitions must use exactly these identifiers —
// this table is authoritative (§2 Game Center checklist). coop_clear is
// deliberately absent: touch builds have no proven local-2P path, so its
// definition and mapping wait for netplay (unmapped earns drop silently
// and re-fire in a future co-op game once the mapping exists).
struct Mapping {
  const char *symbolic;
  const char *gc;
};
const Mapping MAPPINGS[] = {
  { "first_kill",           "cc.gfm.newtonia.first_kill" },
  { "clear_level1",         "cc.gfm.newtonia.clear_level1" },
  { "specials_7",           "cc.gfm.newtonia.specials_7" },
  { "black_hole_survivor",  "cc.gfm.newtonia.black_hole_survivor" },
  { "mini_station_kill",    "cc.gfm.newtonia.mini_station_kill" },
  { "shield_ram",           "cc.gfm.newtonia.shield_ram" },
  { "shield_ram_asteroid",  "cc.gfm.newtonia.shield_ram_asteroid" },
  { "station_destroyed",    "cc.gfm.newtonia.station_destroyed" },
  { "enemies_10",           "cc.gfm.newtonia.enemies_10" },
  { "nova_detonated",       "cc.gfm.newtonia.nova_detonated" },
  { "no_damage_clear",      "cc.gfm.newtonia.no_damage_clear" },
  { "no_secondary_level10", "cc.gfm.newtonia.no_secondary_level10" },
  { "weapons_7",            "cc.gfm.newtonia.weapons_7" },
  { "kills_1000",           "cc.gfm.newtonia.kills_1000" },
  { "kills_10000_lifetime", "cc.gfm.newtonia.kills_10000_lifetime" },
  { "score_3m",             "cc.gfm.newtonia.score_3m" },
  { "reach_level15",        "cc.gfm.newtonia.reach_level15" },
};

const char *gc_identifier(const char *symbolic) {
  if (!symbolic) return NULL;
  for (const Mapping &m : MAPPINGS)
    if (std::strcmp(m.symbolic, symbolic) == 0) return m.gc;
  return NULL;
}

const char *symbolic_for(const char *gc) {
  if (!gc) return NULL;
  for (const Mapping &m : MAPPINGS)
    if (std::strcmp(m.gc, gc) == 0) return m.symbolic;
  return NULL;
}

// Batch cadence for progress-only reports; new unlocks bypass it. Journal
// persistence means a batch that never gets sent this session simply rides
// the next sign-in/foreground flush.
const time_t SEND_INTERVAL_SECONDS = 60;

// A send whose completion never arrives (wedged network stack) would
// otherwise block delivery for the rest of the session; after this long,
// assume the completion was swallowed and allow a fresh send. Worst case
// is a harmless double-post.
const time_t SEND_STALE_SECONDS = 180;

// Present GameKit's sign-in view controller over the SDL window. The
// authenticateHandler can in principle fire before SDL has created the
// window (init() runs after SDL_CreateWindow in ios_main.mm, so in practice
// a root view controller already exists), so retry until one does.
void present_auth(UIViewController *vc) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  UIViewController *root = nil;
  for (UIWindow *w in UIApplication.sharedApplication.windows)
    if (w.isKeyWindow) { root = w.rootViewController; break; }
  if (!root)
    root = UIApplication.sharedApplication.windows.firstObject.rootViewController;
#pragma clang diagnostic pop
  if (!root) {
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(NSEC_PER_SEC / 2)),
                   dispatch_get_main_queue(), ^{ present_auth(vc); });
    return;
  }
  [root presentViewController:vc animated:YES completion:nil];
}

class GameCenterAchievements {
public:
  void init() {
    GKLocalPlayer *player = GKLocalPlayer.localPlayer;
    player.authenticateHandler = ^(UIViewController *vc, NSError *error) {
      dispatch_async(dispatch_get_main_queue(), ^{
        if (vc) { present_auth(vc); return; }
        if (GKLocalPlayer.localPlayer.isAuthenticated) {
          std::cout << "Game Center authenticated — achievements live"
                    << std::endl;
          authenticated_ = true;
          const char *pid = GKLocalPlayer.localPlayer.teamPlayerID.UTF8String;
          current_player_ = pid ? pid : "unknown";
          confirmed_.clear();  // may be a different player than last session
          // Resolve earns journaled before sign-in (they belong to the
          // device's account) and park any previous account's earns.
          AchievementJournal::player_signed_in(current_player_.c_str());
          // Dev reset (set via the Xcode scheme): wipe this account's Game
          // Center achievements and the local pending journal so the earn
          // flow can be re-tested. Quit and relaunch without the variable
          // afterwards, and delete stats.dat too — the lifetime counters
          // would otherwise re-earn immediately (same caveat as the Steam
          // reset in CLAUDE.md).
          if (std::getenv("NEWTONIA_RESET_GAME_CENTER")) {
            dev_reset();
            return;
          }
          reconcile_and_flush();
        } else {
          authenticated_ = false;
          current_player_.clear();
          std::cout << "Game Center unavailable"
                    << (error ? std::string(" (") +
                                    error.localizedDescription.UTF8String + ")"
                              : "")
                    << " — earns stay journaled" << std::endl;
        }
      });
    };
    // Foreground is the natural connectivity-regained retry point; the
    // journal is the source of truth, so a redundant flush is a no-op.
    [[NSNotificationCenter defaultCenter]
        addObserverForName:UIApplicationDidBecomeActiveNotification
                    object:nil
                     queue:NSOperationQueue.mainQueue
                usingBlock:^(NSNotification *note) {
                  (void)note;
                  send_pending(true);
                }];
    // Persist batched progress-only journal writes before backgrounding.
    [[NSNotificationCenter defaultCenter]
        addObserverForName:UIApplicationWillResignActiveNotification
                    object:nil
                     queue:NSOperationQueue.mainQueue
                usingBlock:^(NSNotification *note) {
                  (void)note;
                  AchievementJournal::flush();
                }];
  }

  void report(const char *symbolic, int pct) {
    if (!gc_identifier(symbolic)) return;  // unmapped (e.g. coop_clear)
    std::map<std::string, int>::const_iterator it = confirmed_.find(symbolic);
    if (it != confirmed_.end() && it->second >= pct) return;
    bool advanced =
        AchievementJournal::record(symbolic, pct, current_player_.c_str());
    send_pending(advanced && pct >= 100);
  }

private:
  void dev_reset() {
    [GKAchievement resetAchievementsWithCompletionHandler:^(NSError *error) {
      dispatch_async(dispatch_get_main_queue(), ^{
        if (error) {
          std::cout << "DEV: Game Center achievements reset FAILED ("
                    << error.localizedDescription.UTF8String << ")"
                    << std::endl;
          return;
        }
        // Drop journaled earns too, or they would re-deliver immediately.
        AchievementJournal::clear();
        std::cout << "DEV: Game Center achievements reset for this account"
                  << std::endl;
      });
    }];
  }

  // On sign-in, load what the server already has so journal entries covered
  // by an earlier session (or another device) confirm away instead of
  // re-sending, then flush the remainder. A failed load just flushes
  // everything — double-posting is harmless.
  void reconcile_and_flush() {
    [GKAchievement loadAchievementsWithCompletionHandler:^(
                       NSArray<GKAchievement *> *achievements, NSError *error) {
      dispatch_async(dispatch_get_main_queue(), ^{
        if (!error) {
          for (GKAchievement *a in achievements) {
            const char *sym = symbolic_for(a.identifier.UTF8String);
            if (!sym) continue;
            remember_confirmed(sym, (int)a.percentComplete);
          }
        }
        send_pending(true);
      });
    }];
  }

  void send_pending(bool force) {
    if (!authenticated_) return;
    if (send_in_flight_) {
      // Self-heal a swallowed completion rather than staying mute all
      // session; the journal makes a double-post harmless.
      if (std::time(NULL) - last_send_ < SEND_STALE_SECONDS) return;
      std::cout << "Game Center send stale — assuming completion was lost"
                << std::endl;
      send_in_flight_ = false;
    }
    if (!force && std::time(NULL) - last_send_ < SEND_INTERVAL_SECONDS) return;

    std::vector<AchievementJournal::Entry> batch;
    NSMutableArray<GKAchievement *> *reports = [NSMutableArray array];
    std::vector<AchievementJournal::Entry> entries =
        AchievementJournal::pending(current_player_.c_str());
    for (size_t i = 0; i < entries.size(); i++) {
      const AchievementJournal::Entry &e = entries[i];
      std::map<std::string, int>::const_iterator it = confirmed_.find(e.id);
      if (it != confirmed_.end() && it->second >= e.pct) {
        AchievementJournal::confirm(e.id.c_str(), e.pct,
                                    current_player_.c_str());
        continue;  // server already has it
      }
      const char *gc = gc_identifier(e.id.c_str());
      if (!gc) continue;
      GKAchievement *a =
          [[GKAchievement alloc] initWithIdentifier:@(gc)];
      a.percentComplete = e.pct;
      a.showsCompletionBanner = YES;
      [reports addObject:a];
      batch.push_back(e);
    }
    if (batch.empty()) return;

    send_in_flight_ = true;
    last_send_ = std::time(NULL);
    [GKAchievement reportAchievements:reports
                withCompletionHandler:^(NSError *error) {
      dispatch_async(dispatch_get_main_queue(), ^{
        if (error) {
          // Entries stay journaled; the next unlock/foreground/sign-in
          // retries them. No chained retry here — that would tight-loop
          // while offline.
          send_in_flight_ = false;
          std::cout << "Game Center report failed ("
                    << error.localizedDescription.UTF8String
                    << ") — earns stay journaled" << std::endl;
          return;
        }
        verify_and_confirm(batch);
      });
    }];
  }

  // A nil report error is necessary but not sufficient: the server can
  // accept a batch while silently dropping an identifier that is not
  // defined in App Store Connect (a typo, or the pre-go-live window).
  // Confirm entries out of the journal only when the server's achievement
  // list shows them; anything missing stays journaled and is logged so a
  // portal misconfiguration is visible instead of silently eating earns.
  //
  // Takes the batch BY VALUE: it escapes into the asynchronous completion
  // blocks below, and Objective-C++ blocks capture C++ references by
  // reference (only non-reference variables get the const-copy treatment).
  // A reference parameter here dangles the moment the caller's completion
  // block is destroyed — long before GameKit's load completes — which
  // crashed the first delivery verification (the first_kill unlock).
  void verify_and_confirm(std::vector<AchievementJournal::Entry> batch) {
    [GKAchievement loadAchievementsWithCompletionHandler:^(
                       NSArray<GKAchievement *> *achievements, NSError *error) {
      dispatch_async(dispatch_get_main_queue(), ^{
        send_in_flight_ = false;
        std::map<std::string, int> server;  // symbolic -> pct on server
        if (!error) {
          for (GKAchievement *a in achievements) {
            const char *sym = symbolic_for(a.identifier.UTF8String);
            if (!sym) continue;
            int pct = (int)a.percentComplete;
            int &best = server[sym];
            if (pct > best) best = pct;
          }
        }
        for (size_t i = 0; i < batch.size(); i++) {
          const AchievementJournal::Entry &e = batch[i];
          // If the verification load itself failed, trust the nil report
          // error rather than re-sending forever on a flaky connection.
          std::map<std::string, int>::const_iterator it = server.find(e.id);
          bool delivered = error != nil ||
                           (it != server.end() && it->second >= e.pct);
          if (delivered) {
            remember_confirmed(e.id.c_str(), e.pct);
            AchievementJournal::confirm(e.id.c_str(), e.pct,
                                        current_player_.c_str());
          } else if (verify_warned_.insert(e.id).second) {
            std::cout << "Game Center accepted '" << e.id
                      << "' but does not list it — identifier missing from "
                         "App Store Connect? Earn stays journaled."
                      << std::endl;
          }
        }
        // An unlock that raced this batch shouldn't wait for the next
        // earn/foreground/interval to get its banner — chain one more
        // send. Progress-only leftovers keep riding the interval.
        std::vector<AchievementJournal::Entry> rest =
            AchievementJournal::pending(current_player_.c_str());
        for (size_t i = 0; i < rest.size(); i++)
          if (rest[i].pct >= 100 && !verify_warned_.count(rest[i].id)) {
            send_pending(true);
            break;
          }
      });
    }];
  }

  void remember_confirmed(const char *symbolic, int pct) {
    int &best = confirmed_[symbolic];
    if (pct > best) best = pct;
  }

  bool authenticated_ = false;
  bool send_in_flight_ = false;
  time_t last_send_ = 0;
  std::string current_player_;            // teamPlayerID; "" until signed in
  std::map<std::string, int> confirmed_;  // best percent the server has
  std::set<std::string> verify_warned_;   // portal-misconfig log dedupe
};

GameCenterAchievements *instance() {
  // Constructed on first use and intentionally never destroyed — GameKit
  // blocks capture it and may complete during teardown; process exit
  // reclaims it (same pattern as the Steam backend).
  static GameCenterAchievements *g = new GameCenterAchievements();
  return g;
}

} // namespace

namespace Achievements {
namespace Backend {

// Called from Achievements::init(). Setting the authenticateHandler is what
// kicks off GameKit sign-in; it must be in place before the app settles or
// iOS may not prompt at all, so init runs during startup in ios_main.mm.
void init() {
  instance()->init();
}

void unlock(const char *id) {
  instance()->report(id, 100);
}

void progress(const char *id, int pct) {
  // Range already enforced by the shared seam (achievements.cpp).
  instance()->report(id, pct);
}

} // namespace Backend
} // namespace Achievements

#endif // GAME_CENTER_BUILD
