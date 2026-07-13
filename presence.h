#pragma once

// Platform-neutral online-status ("rich presence") seam, following the
// achievements seam pattern (achievements.h): game code reports what the
// player is doing and each platform backend renders that into the player's
// online status — e.g. "Level 3 Co-Op" in the Steam friends list. The
// default backend is a no-op, so builds without a platform integration
// carry zero cost and zero new dependencies. Backends: Steam
// (steam_presence.cpp under STEAM_BUILD); GDK / Play Games / Game Center
// can slot in later behind their own build flags.
//
// Levels are reported as DISPLAYED level numbers (internal generation + 1),
// the same terminology rule as achievements (ACHIEVEMENTS.md §5).

namespace Presence {

// Browsing the menu (attract screen, main menu, options).
void set_menu();

// In the netplay lobby: hosting a room and waiting for a friend to join, or
// joining someone else's room (entering/submitting a code). Rendered as
// "Hosting a Co-Op Game" / "Joining a Co-Op Game" in the friends list.
void set_hosting();
void set_joining();

// In-game on the given displayed level; num_players >= 2 renders as co-op.
// Safe to re-report the current state — duplicate updates are dropped in
// the shared layer, so callers don't need to track what was last sent.
void set_level(int level, int num_players);

// Clear the status. Call at shutdown; platforms also clear automatically
// when the process exits, so this is best-effort tidiness.
void clear();

} // namespace Presence
