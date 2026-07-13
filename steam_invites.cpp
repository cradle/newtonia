// Steam invite backend for the Invites seam (invites.h) — compiled only when
// STEAM_BUILD is defined. Steam's invite mechanism IS rich presence: a
// non-empty "connect" key gives friends a "Join Game" option in the overlay
// and friends list and enables the invite button. On accept Steam either
//   - launches the game with that string on the command line (cold launch —
//     handled by Invites::capture_launch scanning argv), or
//   - posts GameRichPresenceJoinRequested_t with the string (game already
//     running — handled by the callback below).
// Either way the string is "+connect <roomcode>"; the shared layer
// (Invites::note_accepted) extracts the code and NetLobby joins it. Steam
// only ferries the code — the connection still runs over our signaling +
// WebRTC path, so none of Steam's networking is involved.
//
// Follows https://partner.steamgames.com/doc/api/ISteamFriends
// ("Rich Presence" → the reserved "connect" key).

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
  SteamInvites() : join_cb_(this, &SteamInvites::on_join_requested) {}

  void on_join_requested(GameRichPresenceJoinRequested_t *cb) {
    if (cb) Invites::note_accepted(cb->m_rgchConnect);
  }

private:
  CCallback<SteamInvites, GameRichPresenceJoinRequested_t> join_cb_;
};

SteamInvites *instance() {
  static SteamInvites *s = new SteamInvites();
  return s;
}

} // namespace

namespace Invites {
namespace Backend {

void init() {
  instance();  // register the join-requested callback before the first pump
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
