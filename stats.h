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

// Bits in the special-asteroid kill mask, one per special type ever destroyed
// by a local player (drives the specials_7 achievement).
enum Special {
  SPECIAL_REFLECTIVE  = 0,
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

void add_kill();                    // one asteroid kill; disk writes are batched
void note_special_kill(Special s); // writes through immediately if new
void flush();                       // persist pending changes to stats.dat now

} // namespace Stats
