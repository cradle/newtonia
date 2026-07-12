// Steamworks achievements backend (ACHIEVEMENTS.md §2) — compiled only when
// STEAM_BUILD is defined (set by the deploy-steam workflow, which also puts
// the Steamworks SDK on the include/link path).
//
// Follows https://partner.steamgames.com/doc/features/achievements:
// - Unlocks: SetAchievement() + StoreStats(); the overlay draws the toast.
//   An in-memory unlocked cache keeps the seam's idempotent re-unlocks from
//   spamming StoreStats(), and SetAchievement state survives a failed store
//   (Steam keeps it in-memory and the next StoreStats uploads it).
// - Progress: the seam's 0-100 percent is written to a per-achievement
//   increment-only INT stat (SetStat is documented as cheap/in-memory);
//   the portal binds each stat to its achievement as the "Progress Stat"
//   with unlock value 100, giving Community progress bars, auto-unlock at
//   the threshold, and higher-value merging across devices. StoreStats()
//   is throttled here to checkpoint frequency per Valve's guidance.
// - Offline: "Steam keeps a local cache of the stats and achievement data
//   so that the APIs can be used as normal in offline mode" — no journal
//   needed on this platform (§2 "Offline earns").
// - Requires SDK 1.61+: user stats are requested automatically at
//   SteamAPI_Init() (RequestCurrentStats() was removed). Anything set
//   before UserStatsReceived_t fires is queued and flushed by the callback.

#ifdef STEAM_BUILD

#include <steam/steam_api.h>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <map>
#include <set>
#include <string>

namespace {

// Symbolic ID (ACHIEVEMENTS.md §5) → Steamworks achievement API name and,
// for counter-backed achievements, the progress-stat API name (NULL for
// event-only achievements). The portal definitions must use exactly these
// names — this table is authoritative (§2 Steam checklist).
struct Mapping {
  const char *symbolic;
  const char *ach;
  const char *stat;  // increment-only INT, 0-100, bound as Progress Stat
};
const Mapping MAPPINGS[] = {
  { "first_kill",           "ACH_FIRST_KILL",           NULL },
  { "clear_level1",         "ACH_CLEAR_LEVEL1",         NULL },
  { "specials_7",           "ACH_SPECIALS_7",           "specials_7_pct" },
  { "black_hole_survivor",  "ACH_BLACK_HOLE_SURVIVOR",  NULL },
  { "mini_station_kill",    "ACH_MINI_STATION_KILL",    NULL },
  { "shield_ram",           "ACH_SHIELD_RAM",           NULL },
  { "shield_ram_asteroid",  "ACH_SHIELD_RAM_ASTEROID",  NULL },
  { "station_destroyed",    "ACH_STATION_DESTROYED",    NULL },
  { "enemies_10",           "ACH_ENEMIES_10",           "enemies_10_pct" },
  { "nova_detonated",       "ACH_NOVA_DETONATED",       NULL },
  { "no_damage_clear",      "ACH_NO_DAMAGE_CLEAR",      NULL },
  { "no_secondary_level10", "ACH_NO_SECONDARY_LEVEL10", NULL },
  { "weapons_7",            "ACH_WEAPONS_7",            "weapons_7_pct" },
  { "coop_clear",           "ACH_COOP_CLEAR",           NULL },
  { "kills_1000",           "ACH_KILLS_1000",           "kills_1000_pct" },
  { "kills_10000_lifetime", "ACH_KILLS_10000_LIFETIME", "kills_10000_lifetime_pct" },
  { "score_3m",             "ACH_SCORE_3M",             "score_3m_pct" },
  { "reach_level15",        "ACH_REACH_LEVEL15",        "reach_level15_pct" },
};

const Mapping *find_mapping(const char *symbolic) {
  for (const Mapping &m : MAPPINGS)
    if (std::strcmp(m.symbolic, symbolic) == 0) return &m;
  return NULL;  // unknown symbol: drop silently (new ID without a mapping yet)
}

// Valve: SetStat/SetAchievement are cheap in-memory writes; StoreStats
// uploads and is meant for checkpoints. Unlocks store immediately (the
// toast should be instant); stat progress batches until this many seconds
// have passed since the last store. Anything still pending simply rides
// the next store from any path — including Steam's own shutdown flush.
const time_t STORE_INTERVAL_SECONDS = 60;

class SteamAchievements {
public:
  SteamAchievements() :
      stats_received_(false),
      last_store_time_(0),
      stats_dirty_(false),
      stats_received_cb_(this, &SteamAchievements::on_stats_received),
      stats_stored_cb_(this, &SteamAchievements::on_stats_stored) {}

  void unlock(const char *ach) {
    if (unlocked_.count(ach)) return;
    if (!stats_received_ && !stats_ready_probe()) {
      pending_unlocks_.insert(ach);
      return;
    }
    set_and_store(ach);
  }

  void progress(const char *stat, int pct) {
    if (!stats_received_ && !stats_ready_probe()) {
      int &pending = pending_stats_[stat];
      if (pct > pending) pending = pct;
      return;
    }
    write_stat(stat, pct);
    store_if_due();
  }

  // Primary readiness path on SDK 1.61+: current-user stats are available
  // synchronously once SteamAPI_Init() returns (RequestCurrentStats was
  // removed), and UserStatsReceived_t is not posted for them — observed
  // live: the callback never fired while a GetAchievement probe succeeded.
  // GetAchievement only succeeds once stats are in, so a positive probe
  // means ready. Called at init, and again from unlock/progress as a
  // self-heal if init ran before Steam was reachable.
  bool stats_ready_probe() {
    if (stats_received_) return true;
    ISteamUserStats *stats = SteamUserStats();
    if (!stats) return false;
    bool dummy = false;
    if (!stats->GetAchievement(MAPPINGS[0].ach, &dummy)) return false;
    std::cout << "Steam user stats available — achievements live" << std::endl;
    stats_received_ = true;
    flush_pending();
    return true;
  }

private:

  void flush_pending() {
    for (std::map<std::string, int>::const_iterator it = pending_stats_.begin();
         it != pending_stats_.end(); ++it) {
      write_stat(it->first, it->second);
    }
    pending_stats_.clear();
    for (std::set<std::string>::const_iterator it = pending_unlocks_.begin();
         it != pending_unlocks_.end(); ++it) {
      set_and_store(*it);  // stores at most once more than needed; harmless
    }
    pending_unlocks_.clear();
    store_if_due();
  }
  void set_and_store(const std::string &ach) {
    ISteamUserStats *stats = SteamUserStats();
    if (!stats) return;  // Steam client not running: nothing to deliver to
    bool achieved = false;
    if (stats->GetAchievement(ach.c_str(), &achieved) && achieved) {
      unlocked_.insert(ach);  // already earned on a previous run
      return;
    }
    if (stats->SetAchievement(ach.c_str())) {
      unlocked_.insert(ach);
      store_now();  // unlock toasts should appear immediately
    } else {
      std::cout << "Steam SetAchievement rejected '" << ach
                << "' — name not in the published portal schema?" << std::endl;
    }
  }

  // Raise the increment-only progress stat to pct (never lowers it — the
  // portal marks these stats increment-only as a second guard).
  void write_stat(const std::string &stat, int pct) {
    ISteamUserStats *stats = SteamUserStats();
    if (!stats) return;
    if (pct > 100) pct = 100;
    int current = 0;
    if (stats->GetStat(stat.c_str(), &current) && current >= pct) return;
    if (stats->SetStat(stat.c_str(), pct))
      stats_dirty_ = true;
    else
      std::cout << "Steam SetStat rejected '" << stat
                << "' — name/type not in the published portal schema?"
                << std::endl;
  }

  void store_now() {
    ISteamUserStats *stats = SteamUserStats();
    if (!stats) return;
    if (stats->StoreStats()) {
      stats_dirty_ = false;
      last_store_time_ = std::time(NULL);
    }
  }

  void store_if_due() {
    if (!stats_dirty_) return;
    if (std::time(NULL) - last_store_time_ < STORE_INTERVAL_SECONDS) return;
    store_now();
  }

  void on_stats_received(UserStatsReceived_t *result) {
    if (result->m_eResult != k_EResultOK) {
      // Without this callback succeeding nothing can ever reach Steam —
      // pending unlocks/stats stay queued until process exit.
      std::cout << "Steam UserStatsReceived failed (result "
                << result->m_eResult << ") — achievements cannot sync"
                << std::endl;
      return;
    }
    if (stats_received_) return;  // probe already went live
    std::cout << "Steam user stats received — achievements live" << std::endl;
    stats_received_ = true;
    flush_pending();
  }

  void on_stats_stored(UserStatsStored_t *result) {
    // k_EResultInvalidParam means a stat/achievement name isn't published in
    // the portal — surface it, since the earn would be silently lost.
    if (result->m_eResult != k_EResultOK)
      std::cout << "Steam StoreStats failed (result " << result->m_eResult
                << ") — check portal stat/achievement definitions" << std::endl;
  }

  bool stats_received_;
  time_t last_store_time_;
  bool stats_dirty_;
  std::set<std::string> unlocked_;
  std::set<std::string> pending_unlocks_;
  std::map<std::string, int> pending_stats_;  // stat name → best pct so far
  CCallback<SteamAchievements, UserStatsReceived_t> stats_received_cb_;
  CCallback<SteamAchievements, UserStatsStored_t> stats_stored_cb_;
};

SteamAchievements *instance() {
  // Constructed on first use — after steam_init() — and intentionally never
  // destroyed (Steam callbacks must not outlive SteamAPI_Shutdown teardown
  // ordering; process exit reclaims it).
  static SteamAchievements *s = new SteamAchievements();
  return s;
}

} // namespace

namespace Achievements {
namespace Backend {

// Called from Achievements::init() right after SteamAPI_Init(). On SDK
// 1.61+ current-user stats are ready synchronously by now, so the probe
// normally goes live immediately; the CCallbacks registered by the
// constructor cover any path where they are not (and must be registered
// before the first SteamAPI_RunCallbacks(), hence init-time construction
// rather than lazily on the first unlock).
void init() {
  // Dev reset: NEWTONIA_RESET_STEAM_STATS=1 wipes this account's
  // achievements and progress stats so the earn flow can be re-tested
  // (Valve provides no portal button for per-user resets; ResetAllStats
  // from the game is the sanctioned way). Self-targeting only — a player
  // setting it merely resets their own earns. Quit and relaunch without
  // the variable afterwards; in-game counters in a live session would
  // otherwise re-earn immediately.
  if (std::getenv("NEWTONIA_RESET_STEAM_STATS")) {
    ISteamUserStats *stats = SteamUserStats();
    if (stats && stats->ResetAllStats(true) && stats->StoreStats())
      std::cout << "DEV: Steam achievements and stats reset for this account"
                << std::endl;
    else
      std::cout << "DEV: Steam achievements reset FAILED (client not running?)"
                << std::endl;
  }
  instance()->stats_ready_probe();
}

void unlock(const char *id) {
  const Mapping *m = find_mapping(id);
  if (!m) return;
  instance()->unlock(m->ach);
}

void progress(const char *id, int pct) {
  const Mapping *m = find_mapping(id);
  if (!m || !m->stat) return;  // event-only achievement: nothing to report
  instance()->progress(m->stat, pct);
}

} // namespace Backend
} // namespace Achievements

#endif // STEAM_BUILD
