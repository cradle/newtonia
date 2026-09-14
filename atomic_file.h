#ifndef ATOMIC_FILE_H
#define ATOMIC_FILE_H

#include <cstdio>
#include <functional>
#include <string>

// Checked, atomic replacement of the small data files in the pref path —
// savegame.dat, stats.dat, pending_achievements.dat, highscore.dat, the
// preferences INI and the replay slots. One primitive so no writer can
// drift back to the pattern this replaced: fopen(path, "wb") truncates the
// last good copy BEFORE the new bytes exist, a buffered fwrite succeeds and
// only fclose reports the full disk, and a file the reader takes by fread
// success (stats.dat) makes a truncated write indistinguishable from an
// older, shorter one — zeroed counters that roam to every install via Steam
// Auto-Cloud (security review 2026-09-08, F5/F6/F8).
namespace AtomicFile {

// The private temporary a write goes to: beside `path`, so the replace is
// a same-filesystem rename, and unique per PROCESS, so two instances
// sharing one pref path (the netplay e2e drivers, two builds on one
// machine) never open the same temporary — with a shared name one renamed
// the other's half-written file into place and kept writing to it.
std::string temp_sibling(const std::string &path);

// Creates the temporary, hands it to `write`, closes it (checked — this is
// where a full disk surfaces), and renames it over `path`. On ANY failure
// the temporary is removed, `path` keeps its previous contents, the failure
// is logged under `what`, and false is returned so the caller can keep its
// dirty state for a retry. `write` returns false for a failure of its own.
bool write(const std::string &path,
           const std::function<bool(FILE *)> &write, const char *what);

// Rename with replace semantics everywhere: the Windows CRT rename refuses
// an existing destination, so it goes through MoveFileEx there.
bool replace(const std::string &from, const std::string &to);

}  // namespace AtomicFile

#endif
