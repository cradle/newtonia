#pragma once

// Platform-neutral achievements seam — see ACHIEVEMENTS.md §3.
//
// Call sites use stable symbolic IDs from the §5 master list; each platform
// backend (GDK, Steamworks, Play Games Services, Game Center) owns its own
// symbolic→platform-ID mapping table, so renaming in any store portal never
// touches game code. The default backend is a no-op, so builds without a
// platform integration carry zero cost and zero new dependencies.
//
// Symbolic IDs currently wired up (ACHIEVEMENTS.md §5):
//   first_kill, clear_gen1, all_specials, black_hole_survivor,
//   mini_station_kill, station_destroyed, nova_full, no_damage_clear,
//   all_weapons, coop_clear, kills_100, kills_1000_lifetime, reach_gen25

namespace Achievements {

// Unlock an achievement. Safe to call repeatedly — backends treat unlocks as
// idempotent. Suppressed while a cheat is active this generation (XR-057).
void unlock(const char *id);

// Report progress toward an achievement as a percentage (1–100); 100 unlocks.
// Out-of-range values are clamped. Backends only ever move progress forward,
// so callers may re-report the current total on every increment.
void progress(const char *id, int pct);

// ── XR-057 cheat suppression (ACHIEVEMENTS.md §1) ───────────────────────────
// Lives in this shared layer so every backend inherits it: any cheat input
// (skip-level, time-scale keys) suppresses all unlocks and progress until the
// next legitimately started generation.

void note_cheat_used();     // a cheat key changed the game state
void generation_started();  // a generation legitimately begins: lift suppression
bool unlocks_suppressed();  // true while suppression is active

} // namespace Achievements
