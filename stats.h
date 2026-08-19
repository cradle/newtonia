#pragma once
#include <cstdint>

// Lifetime gameplay stats backing the long-horizon achievements — see
// ACHIEVEMENTS.md §4. Deliberately a standalone roaming stats.dat next to
// highscore.dat rather than fields inside savegame.dat, so the counters keep
// counting in modes that never touch the campaign save (e.g. the planned
// netplay) and roam with the account on platforms with roaming storage.
//
// Attribution rule (§4): callers record only kills credited to a *local*
// player — remote netplay peers count their own kills on their own machine.

namespace Stats {

// Bits in the killable-asteroid-type kill mask, one per type ever destroyed
// by a local player (drives the specials_7 achievement). Reflective and
// invincible asteroids are deliberately absent: they die only to god mode,
// so they are bonus kills rather than requirements.
enum Special {
  SPECIAL_NORMAL      = 0,
  SPECIAL_TELEPORTING = 1,
  SPECIAL_INVISIBLE   = 2,
  SPECIAL_QUANTUM     = 3,
  SPECIAL_TOUGH       = 4,
  SPECIAL_ARMOURED    = 5,
  SPECIAL_PHASING     = 6,
  SPECIAL_COUNT       = 7
};

uint32_t lifetime_kills();     // asteroids ever destroyed by local players
uint32_t special_kill_mask();  // bitmask of Special values ever destroyed
// Primary-weapon discharges by local players (v2 field; a v1 file reads
// back 0). One per DISCHARGE: a scatter level is one shot however many
// barrels it mints, each burst emission is its own, and beam/lance/shock
// bolts count one each. Turret-drone bullets and secondaries are
// deliberately excluded — the companion ACCURACY readout is about the
// pilot's own trigger.
uint32_t shots_fired();
// v3 fields (a v2 file reads them back 0). All share the attribution
// rule and the cheat freeze; the callers' gate is
// is_local_player && !net_remote_gun (replay playback marks every ship
// net_remote_gun, including the recorder's own seat, so watching can
// never bank anything).
uint32_t ship_kills();         // enemy ships destroyed (friendly fire excluded)
uint32_t deaths();             // times a local ship was destroyed
uint32_t games_played();       // fresh NEW GAME starts (continues resume one)
uint32_t highest_level();      // max displayed level ever reached (gen + 1)
uint32_t play_seconds();       // active play time (not paused/menus/replays)
uint32_t secondaries_used();   // successful secondary activations (no nova)
uint32_t novas_detonated();

void add_kill();                    // one asteroid kill; disk writes are batched
void add_shot();                    // one primary discharge; writes are batched
void note_special_kill(Special s); // writes through immediately if new
void add_ship_kill();               // batched (waves come in bursts)
void add_death();                   // rare: writes through
void add_game_played();             // rare: writes through
void note_level_reached(uint32_t displayed_level);  // writes through on a new max
void add_play_time(int ms);         // accumulates; writes every ~60 s accrued
void add_secondary_used();          // batched
void add_nova();                    // rare: writes through
void flush();                       // persist pending changes to stats.dat now

} // namespace Stats
