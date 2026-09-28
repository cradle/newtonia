#pragma once
#include <cstdint>

// Cloud sync of the three files that roam — savegame.dat, stats.dat and
// highscore.dat, exactly the set Steam Auto-Cloud carries (CLAUDE.md
// "Steam Cloud"). Replays, preferences and the online/netplay files stay on
// the device: replays are megabytes, and the rest describe the device.
//
// The only backend is iOS (ios_cloud_sync.mm, the iCloud key-value store);
// cloud_sync.cpp is the no-op everywhere else, so the writers call these
// hooks unconditionally, like web_fs_sync. The game keeps reading and
// writing its plain files: the backend merges the cloud copy INTO those
// files (never the other way round mid-write), so no reader changes.
//
// Merge rules (cloud_sync_merge.h, unit-tested):
//   highscore.dat — the higher score wins.
//   stats.dat     — per counter the higher value, the special-kill mask
//                   OR'd. Counters only ever grow, so max never loses a
//                   device's progress to another's older copy (two devices
//                   played apart keep the larger total, not the sum).
//   savegame.dat  — one run in progress: the newest write wins, deletes
//                   included (a game over on one device must not resurrect
//                   the run on another), by a wall-clock stamp per write.
namespace CloudSync {

enum File { SAVEGAME = 0, STATS = 1, HIGHSCORE = 2 };

// Start the backend and merge the cloud copy into the local files. Call
// before anything reads them (before the first Menu is built).
void init();

// Once per frame: applies changes another device pushed since the last
// call. Returns true when a local file changed, so a screen that cached a
// value (the menu's CONTINUE row, the high score) can re-read it.
bool poll();

// A counter bumped every time poll()/init() changed a local file; screens
// compare it to the value they last saw.
uint32_t local_generation();

// After a successful local write (or, for SAVEGAME, a delete): push it.
void local_written(File f);

// App lifecycle: flush on the way out, re-check on the way back.
void app_background();
void app_foreground();

}  // namespace CloudSync
