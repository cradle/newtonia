// Steam backend for the netplay peer-identity seam (net_identity.h) —
// compiled only when STEAM_BUILD is defined, like steam_presence.cpp.
//
// Supplies the platform tag and the local display name: the persona name
// Steam already shows publicly. Deliberately NOT the SteamID — no platform
// account IDs travel on the wire or reach the logs (net_identity.h). The
// shared layer sanitizes and caps whatever this returns.

#ifdef STEAM_BUILD

#include <steam/steam_api.h>

#include <string>

#include "net_identity.h"

namespace NetIdentityBackend {

uint8_t local_platform() { return NET_PLATFORM_STEAM; }

std::string local_name() {
  ISteamFriends *friends = SteamFriends();
  if (!friends) return "";  // Steam client not running: generic fallback
  const char *persona = friends->GetPersonaName();
  return persona ? persona : "";
}

}  // namespace NetIdentityBackend

#endif  // STEAM_BUILD
