#pragma once

// Platform-neutral achievements seam — see ACHIEVEMENTS.md §3.
//
// Call sites use stable symbolic IDs from the §5 master list; each platform
// backend (GDK, Steamworks, Play Games Services, Game Center) owns its own
// symbolic→platform-ID mapping table, so renaming in any store portal never
// touches game code. The default backend is a no-op, so builds without a
// platform integration carry zero cost and zero new dependencies.
//
// Symbolic IDs currently wired up (ACHIEVEMENTS.md §5; user-facing text uses
// displayed level numbers, i.e. internal generation + 1):
//   first_kill, clear_level1, specials_7, black_hole_survivor,
//   mini_station_kill, shield_ram, shield_ram_asteroid, station_destroyed,
//   enemies_10, nova_detonated, no_damage_clear, no_secondary_level10,
//   weapons_7, coop_clear, kills_1000, kills_10000_lifetime, score_3m,
//   reach_level15

namespace Achievements {

// Initialise the platform backend. Call once at startup, right after the
// platform's own init (e.g. SteamAPI_Init) and BEFORE the first frame: the
// Steam backend must register its stat callbacks before the first
// SteamAPI_RunCallbacks(), or the SDK's automatic stats delivery is
// dispatched with no listener and every earn queues forever. No-op on
// builds without a platform backend.
void init();

// Unlock an achievement. Safe to call repeatedly — backends treat unlocks as
// idempotent. Suppressed while a cheat is active this generation (XR-057).
void unlock(const char *id);

// Report progress toward an achievement as a percentage (1–100); 100 unlocks.
// Out-of-range values are clamped. Backends only ever move progress forward,
// so callers may re-report the current total on every increment.
void progress(const char *id, int pct);

// ── XR-057 cheat suppression (ACHIEVEMENTS.md §1) ───────────────────────────
// Lives in this shared layer so every backend inherits it: any cheat input
// (skip-level, time-scale keys) suppresses all unlocks and progress for the
// REST OF THE GAME — a per-generation reset would let a player skip to one
// generation short of a progression achievement and legitimately clear a
// single level to unlock it. The flag is persisted in the savegame, so
// save/quit/resume doesn't launder it; only starting a new game clears it.

void note_cheat_used();     // a cheat key changed the game state
void new_game_started();    // a fresh game begins: lift suppression
bool unlocks_suppressed();  // true while suppression is active (saved/restored)

} // namespace Achievements
