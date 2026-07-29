#ifndef WEB_FS_H
#define WEB_FS_H

// Flush the in-memory filesystem to IndexedDB (web only; a no-op everywhere
// else, so callers need no #ifdef).
//
// Every persistent write on web has to end in FS.syncfs, and there are seven
// places that persist something — savegame, preferences, replay chunks and
// header patches, lifetime stats, high score, the netplay resume token. They
// all share ONE filesystem, and they fire in the same frame constantly: a
// pause writes the savegame, flushes stats and flushes the replay, one after
// the other.
//
// Independent syncfs calls do not survive that. IDBFS enumerates the local
// tree, then reads each file inside an async IndexedDB transaction — so a
// sync still in flight while a later one renames or deletes a file (exactly
// what the current -> recent rotation does) reads an entry that is no longer
// there and fails the whole batch: "[newtonia] replay sync failed: ErrnoError"
// after every rotation, observed in-container 2026-07-29.
//
// So serialize them. At most one sync is ever in flight; anything requested
// while one is running sets a flag, and exactly one more sync runs when it
// finishes — which is what makes this safe rather than merely quiet, since
// that trailing run sees the final state of every write that arrived during
// the first. `why` names the most recent requester, for the error line only.
void web_fs_sync(const char *why);

#endif  // WEB_FS_H
