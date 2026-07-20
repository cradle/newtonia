#ifndef NET_POLICY_H
#define NET_POLICY_H

// Netplay permission policy — platform-neutral seam following the
// presence/invites/identity pattern: the default backend allows everything,
// so no platform's behavior changes by shipping this. A platform whose
// policy can forbid online play or cross-network communication replaces the
// backend behind its own build flag (the Xbox fork's private backend
// implements XUserCheckPrivilege — multiplayer sessions privilege 254,
// cross-network communication privilege 185, both from the public GDK
// docs — behind this seam; upstream ships the seam only).

#include "net_identity.h"

// May this player go online at all? Gates the menu's ONLINE row (ANDed
// with net_available()) and the lobby's HOST/JOIN commit paths.
bool net_online_play_allowed();

// May this player play with THIS peer, now that the handshake told us who
// they are? Checked when a session reaches Ready (lobby connect and host
// mid-game rejoin); a refusal tears the session down.
bool net_comms_allowed_with(const NetIdentity &peer);

#endif /* NET_POLICY_H */
