#ifndef NET_RESUME_H
#define NET_RESUME_H

// Host process-death resume (NETPLAY.md). The M3-1 reclaim token lives
// only in the hosting process's memory: backgrounding and wifi blips are
// covered, but if the OS kills the app the token — and the world — die
// with it. This module persists the session ticket {room code, reclaim
// token, PROTO_VERSION, timestamp} to a small file in the SDL pref path,
// beside (never inside) the solo save, so a relaunch within the relay's
// reclaim grace can offer "RESUME HOSTING <CODE>" on the menu. The world
// itself rides a dedicated online save slot (Save::online_save_game),
// written at the same checkpoints; the two files live and die together.
//
// The ticket's timestamp is refreshed on a slow cadence by the hosting
// game (the file is ~100 bytes), so its age closely tracks when the host
// actually died — "age < GRACE_MS" then means the room's reclaim grace
// may still be open, not merely that a level was cleared recently.

#include <string>

namespace NetResume {

// Mirrors the signaling worker's HOST_GRACE_MS (signal/src/worker.js):
// how long a hostless room survives awaiting a token reclaim. A ticket
// older than this is a dead room's leftover and is deleted on sight.
extern const long long GRACE_MS;

// Write/refresh the ticket (stamps PROTO_VERSION + now).
bool write(const std::string &room_code, const std::string &room_token);

// Read a ticket written by a build with OUR PROTO_VERSION; false when
// absent, unreadable, or minted by a different protocol (a resumed host
// on a new build could never re-pair with the old session's client
// anyway — the rejoin handshake would refuse).
bool read(std::string &room_code, std::string &room_token,
          long long &age_ms);

void clear();            // delete the ticket file
void clear_with_save();  // ticket + the online save slot (clean session end)

}  // namespace NetResume

#endif
