#pragma once
#include <string>
#include <vector>

// Pending-earns journal for achievement backends whose platform SDK does not
// reliably queue offline earns (ACHIEVEMENTS.md §2 "Offline earns"): Game
// Center today, the GDK backend in the private mirror next. Steam and Play
// Games cache earns client-side, so their backends never touch this.
//
// A backend records every earn here BEFORE attempting delivery and confirms
// it only when the platform acknowledges the post; anything unconfirmed is
// resubmitted on launch, sign-in, and foreground. Resubmission is safe by
// construction — the shared layer guarantees unlocks are idempotent and
// progress is monotonic, so double-posting is harmless on every platform.
//
// Entries are keyed by the platform-neutral SYMBOLIC id (§5 master list)
// plus the platform account that earned them, so the file format is shared
// between backends and earns can never be credited to a different account
// (XR-055: unlocks post to the profile that met them — ACHIEVEMENTS.md §1).
// Earns made before sign-in completes are recorded unowned and resolved by
// player_signed_in(): they belong to the device's last-signed-in account
// (offline sessions still earn for the account configured on the device),
// or to the first account that ever signs in. After an account switch, the
// previous account's pending earns stay parked under its id — delivered if
// it signs back in, never to the new account.
//
// The journal lives in the SDL pref path (pending_achievements.dat) beside
// stats.dat and follows the same append-only versioning convention. Unlock
// records (pct 100 — the earns that cannot be re-derived from counters)
// write through immediately; progress-only advances batch in memory until
// flush(), mirroring stats.cpp's batching so per-kill hooks never hit the
// disk every kill.
//
// Not thread-safe: call everything from one thread (the game/main thread).
// Backends must marshal SDK completion callbacks accordingly (the Game
// Center backend dispatches every completion to the main queue).

namespace AchievementJournal {

struct Entry {
  std::string id;     // symbolic achievement id
  int pct;            // best percent earned so far; 1-100, 100 == unlock
  std::string owner;  // platform account id; "" = awaiting player_signed_in()
};

// Merge an earn into the journal (per id+owner maximum). owner is the
// authenticated platform account id, or "" before sign-in resolves.
// Returns true if this advanced the stored percent — i.e. a genuinely new
// earn, not one of the seam's idempotent re-fires. pct==100 entries persist
// immediately; progress-only advances persist on the next flush().
bool record(const char *id, int pct, const char *owner);

// An account finished signing in: resolve unowned entries (assign them to
// the device's previous account, or to this player if none was recorded)
// and remember this player as the device's account. Call before pending().
void player_signed_in(const char *player);

// Outstanding earns awaiting delivery TO THIS ACCOUNT, for resubmission.
std::vector<Entry> pending(const char *owner);

// The platform confirmed delivery of this account's earn at pct: drop the
// entry unless a higher percent was recorded since the send was snapshotted.
void confirm(const char *id, int pct, const char *owner);

// Persist any batched progress-only advances now.
void flush();

// Drop every entry (dev reset).
void clear();

} // namespace AchievementJournal
