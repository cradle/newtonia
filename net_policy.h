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

// BACKEND CONTRACT — these are HOT-PATH calls. net_online_play_allowed()
// runs several times per frame from the menu's draw/nav paths
// (Menu::show_online_row and friends), and net_comms_allowed_with() runs
// inside the handshake pump. A backend must answer from a cached snapshot
// of the platform's state (refresh it out-of-band: on sign-in, on the
// platform's privilege-change event, or on a timer) — never a blocking
// system/service query per call. The same rule the presence seam applies
// in the other direction (presence.cpp dedupes so backends only hear real
// changes).

// May this player go online at all? Gates the menu's ONLINE row (ANDed
// with net_available()) and the lobby's HOST/JOIN commit paths (including
// the rejoin/invite-accept constructor).
bool net_online_play_allowed();

// May this player play with THIS peer? Enforced at the single chokepoint
// where the peer's identity becomes known — inside NetSession's handshake
// (net_session.cpp): the host checks between parsing the HELLO identity
// and sending WELCOME (refusal = MSG_REJECT RejectNotAllowed, so the peer
// is told honestly); the client checks the WELCOME identity and goes
// Rejected locally. Session adopters (lobby, mid-game rejoin) just see
// Phase Rejected — no per-adopter checks to forget.
bool net_comms_allowed_with(const NetIdentity &peer);

#endif /* NET_POLICY_H */
