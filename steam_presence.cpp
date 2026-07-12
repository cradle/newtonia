// Steamworks rich-presence backend for the Presence seam (presence.h) —
// compiled only when STEAM_BUILD is defined (set by the deploy-steam
// workflow and the local `make steam` target).
//
// Follows https://partner.steamgames.com/doc/features/enhancedrichpresence:
// - "steam_display" selects a localization token uploaded in the Steamworks
//   portal (App Admin → Community → Rich Presence); the token substitutes
//   other rich-presence keys via %key% (here: %level%). The tokens set here
//   must match steam/rich_presence.vdf, which is the in-repo source of
//   truth for the portal upload — a token missing from the portal renders
//   as no enriched status line at all in the friends list.
// - "status" is the plain-English equivalent shown in the friends-list
//   "View Game Info" dialog, and the fallback for clients that predate
//   enhanced rich presence.
// SetRichPresence is a cheap local write (the client batches uploads), so
// no throttling is needed; the shared layer already dedupes repeats.

#ifdef STEAM_BUILD

#include <steam/steam_api.h>
#include <cstdio>

namespace Presence {
namespace Backend {

void set_menu() {
  ISteamFriends *friends = SteamFriends();
  if (!friends) return;  // Steam client not running: nothing to show status on
  friends->SetRichPresence("status", "In the Menu");
  friends->SetRichPresence("steam_display", "#StatusMenu");
}

void set_level(int level, int num_players) {
  ISteamFriends *friends = SteamFriends();
  if (!friends) return;
  char level_str[16];
  std::snprintf(level_str, sizeof(level_str), "%d", level);
  char status[32];
  std::snprintf(status, sizeof(status), "Level %d%s", level,
                num_players >= 2 ? " Co-Op" : "");
  friends->SetRichPresence("level", level_str);
  friends->SetRichPresence("status", status);
  friends->SetRichPresence("steam_display",
                           num_players >= 2 ? "#StatusLevelCoop"
                                            : "#StatusLevel");
}

void clear() {
  ISteamFriends *friends = SteamFriends();
  if (!friends) return;
  friends->ClearRichPresence();
}

} // namespace Backend
} // namespace Presence

#endif // STEAM_BUILD
