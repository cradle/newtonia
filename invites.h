#pragma once

#include <string>

// Platform-neutral game-invite seam, following the presence/achievements
// seam pattern (presence.h, achievements.h). The room code is the universal
// join token: the platform invite system is used ONLY to ferry that code
// from host to joiner (and hand it back on accept) — the actual connection
// still runs over our own signaling + WebRTC path, so no platform networking
// is involved. The default backend is a no-op; Steam slots in behind
// STEAM_BUILD (steam_invites.cpp), and GDK / others can follow behind their
// own build flags.
//
// Flow:
//   host gets a room code   -> Invites::set_joinable(code)   (friends see "Join Game")
//   session fills / host out -> Invites::clear_joinable()
//   friend accepts an invite -> poll_accepted_invite() yields the code
//                               -> the game joins it via NetLobby(code)

namespace Invites {

// Register the platform backend's invite callbacks. Call once at startup,
// right after the platform's own init (e.g. SteamAPI_Init) and BEFORE the
// first callback pump: the Steam backend must register its
// GameRichPresenceJoinRequested_t handler before the first
// SteamAPI_RunCallbacks(), or an invite accepted while the game is already
// running is missed. No-op on builds without a backend.
void init();

// Host: advertise a joinable co-op session carrying this room code, so
// friends get a "Join Game" option in the platform overlay/friends list.
// Safe to call repeatedly.
void set_joinable(const std::string &room_code);

// No longer joinable — the second (only) co-op slot filled, or the host
// left the lobby/game. Removes the friends-list Join option.
void clear_joinable();

// A friend accepted an invite, or the game was cold-launched from one: if a
// room code is pending, move it into code_out and return true (draining it).
// Poll from the menu each frame; feed the code to a new NetLobby(code_out).
bool poll_accepted_invite(std::string &code_out);

// Cold launch: called once at startup with argc/argv so a "+connect <code>"
// the platform appended to the command line becomes a pending accepted
// invite (the running-game path arrives through the backend callback).
void capture_launch(int argc, char **argv);

// Backends call this with the platform "connect" string when a friend
// accepts an invite; the shared layer extracts the room code from it (the
// string is "+connect <code>", but a bare code is tolerated too).
void note_accepted(const char *connect_string);

// Desktop window focus: accepting an invite while the game is already running
// (Steam launches an already-running game via steam://run WITHOUT bringing it
// to the front) sets a one-shot request. The desktop entry point (glut.cpp)
// drains it each frame and re-raises/activates the window. Returns true at
// most once per request; false otherwise. No-op consumer on mobile/web.
bool take_focus_request();

} // namespace Invites
