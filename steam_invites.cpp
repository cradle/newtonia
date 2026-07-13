// Steam invite backend for the Invites seam (invites.h) — compiled only when
// STEAM_BUILD is defined. Steam's invite mechanism IS rich presence: a
// non-empty "connect" key gives friends a "Join Game" option in the overlay
// and friends list and enables the invite button. On accept Steam either
//   - the game is already running -> posts GameRichPresenceJoinRequested_t
//     with the connect string (no prompt), or
//   - the game was closed -> cold-launches it with the connect string. To
//     avoid the OS command-line security dialog ("this game requested to
//     launch with +connect <code>"), we read the string through the Steam
//     API (ISteamApps::GetLaunchCommandLine) rather than scanning argv:
//     Steam then delivers it in-band and skips the prompt, PROVIDED the app
//     is configured on the Steamworks side to route the connect through the
//     API instead of the OS command line (Installation → Launch Options; see
//     the GetLaunchCommandLine docs). The argv scan (Invites::capture_launch)
//     stays as a fallback for builds/config where Steam still uses argv.
// Either way the string is "+connect <roomcode>"; the shared layer
// (Invites::note_accepted) extracts the code and NetLobby joins it. Steam
// only ferries the code — the connection still runs over our signaling +
// WebRTC path, so none of Steam's networking is involved.
//
// Follows https://partner.steamgames.com/doc/api/ISteamFriends
// ("Rich Presence" → the reserved "connect" key) and ISteamApps
// ::GetLaunchCommandLine.

#ifdef STEAM_BUILD

#include "invites.h"

#include <steam/steam_api.h>
#include <string>

namespace {

// Registered once at init so an invite accepted while the game is already
// running (the player never hosted this session, so set_joinable was never
// called) still reaches us. Constructed on first use and intentionally never
// destroyed — Steam callbacks must not outlive SteamAPI_Shutdown teardown
// ordering; process exit reclaims it (same rule as steam_achievements.cpp).
class SteamInvites {
public:
  SteamInvites()
      : join_cb_(this, &SteamInvites::on_join_requested),
        url_cb_(this, &SteamInvites::on_new_url_params) {}

  void on_join_requested(GameRichPresenceJoinRequested_t *cb) {
    if (cb) Invites::note_accepted(cb->m_rgchConnect);
  }

  // A running game handed a fresh launch command line (an API-delivered Join
  // / steam:// while already open) — read it the promptless way.
  void on_new_url_params(NewUrlLaunchParameters_t *) { read_launch_command_line(); }

  // Cold launch: pull the connect string from the Steam API rather than argv
  // so Steam can deliver it without the OS command-line security dialog.
  static void read_launch_command_line() {
    ISteamApps *apps = SteamApps();
    if (!apps) return;
    char cmd[1024] = {0};
    if (apps->GetLaunchCommandLine(cmd, sizeof(cmd)) > 0)
      Invites::note_accepted(cmd);  // parse_connect tolerates the full string
  }

private:
  CCallback<SteamInvites, GameRichPresenceJoinRequested_t> join_cb_;
  CCallback<SteamInvites, NewUrlLaunchParameters_t> url_cb_;
};

SteamInvites *instance() {
  static SteamInvites *s = new SteamInvites();
  return s;
}

} // namespace

namespace Invites {
namespace Backend {

void init() {
  instance();  // register the join/url callbacks before the first pump
  // Cold launch via an invite: the connect string is waiting in the Steam
  // API command line (the promptless path).
  SteamInvites::read_launch_command_line();
}

void set_joinable(const std::string &room_code) {
  ISteamFriends *friends = SteamFriends();
  if (!friends) return;  // Steam client not running
  std::string connect = "+connect " + room_code;
  friends->SetRichPresence("connect", connect.c_str());
}

void clear_joinable() {
  ISteamFriends *friends = SteamFriends();
  if (!friends) return;
  // An empty "connect" value removes the friends-list Join option.
  friends->SetRichPresence("connect", "");
}

} // namespace Backend
} // namespace Invites

#endif // STEAM_BUILD
