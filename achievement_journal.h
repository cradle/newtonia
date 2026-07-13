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
// Entries are keyed by the platform-neutral SYMBOLIC id (§5 master list),
// not platform ids, so the file format is shared between backends. The
// journal lives in the SDL pref path (pending_achievements.dat) beside
// stats.dat and follows the same append-only versioning convention.
//
// Note the journal is per-device, not per-platform-account: earns made
// signed-out are delivered to whoever signs in next. Acceptable on the
// single-account platforms this serves; revisit if a cert audit disagrees.

namespace AchievementJournal {

struct Entry {
  std::string id;  // symbolic achievement id
  int pct;         // best percent earned so far; 1-100, 100 == unlock
};

// Merge an earn into the journal (per-id maximum) and persist it. Returns
// true if this advanced the stored percent — i.e. a genuinely new earn, not
// one of the seam's idempotent re-fires.
bool record(const char *id, int pct);

// The platform confirmed delivery at pct: drop the entry unless a higher
// percent was recorded since the send was snapshotted.
void confirm(const char *id, int pct);

// Outstanding earns awaiting delivery, for resubmission.
std::vector<Entry> pending();

} // namespace AchievementJournal
