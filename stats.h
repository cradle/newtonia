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

void add_kill();                    // one asteroid kill; disk writes are batched
void add_shot();                    // one primary discharge; writes are batched
void note_special_kill(Special s); // writes through immediately if new
void flush();                       // persist pending changes to stats.dat now

} // namespace Stats
