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

// May this player go online at all? Gates the lobby's HOST/JOIN commit
// paths (including the rejoin/invite-accept constructor), the menu's
// online-resume offer and the leaderboard upload. The menu's ONLINE row
// stays visible on a capable build either way: a refusal has to be
// explained where the player asked for the feature, not hidden.
bool net_online_play_allowed();

// The platform's chance to explain a refusal and let the player fix it.
// Called once each time a lobby commit is refused by
// net_online_play_allowed(), beside the lobby's own "ONLINE PLAY NOT
// ALLOWED" screen. A backend whose platform has a system dialog for this
// (an account that lacks a membership or needs a parent's approval) opens
// it here, asynchronously, and refreshes its cached answer when the dialog
// closes; the player then backs out and tries again. Must not block. The
// default backend does nothing.
void net_online_play_explain();

// May this player play with THIS peer? Enforced at the single chokepoint
// where the peer's identity becomes known — inside NetSession's handshake
// (net_session.cpp): the host checks between parsing the HELLO identity
// and sending WELCOME (refusal = MSG_REJECT RejectNotAllowed, so the peer
// is told honestly); the client checks the WELCOME identity and goes
// Rejected locally. Session adopters (lobby, mid-game rejoin) just see
// Phase Rejected — no per-adopter checks to forget.
bool net_comms_allowed_with(const NetIdentity &peer);

// May this player play with this peer, now that the WORKER has said who
// the peer is? net_comms_allowed_with() above only sees the peer's own
// claim; a platform that checks its player's block list and privacy
// settings against a specific account needs the worker's attestation,
// which arrives on the signalling channel, asynchronously, often after
// the p2p handshake (attested.platform_account carries the account id
// where the worker sends one — net_identity.h).
//   ALLOW   play.
//   WAIT    the answer isn't known yet (the attestation hasn't landed, or
//           the platform's own check is still running): ask again shortly.
//           Waiting too long refuses the peer as not allowed.
//   REFUSE  refused by the platform's policy for this peer.
// Asked by the host at both of its doors, inside the handshake (the lobby's
// waiting room and the mid-game rejoin door: NetLobby::admit_verdict, re-
// asked each tick while the WELCOME is held; refusal = MSG_REJECT
// RejectNotAllowed), and by a joining client before it enters the host's
// game (NetLobby's Connected screen). Same hot-path rule: answer from a
// cache, start any service check out of band. The default allows.
enum NetPeerVerdict { NET_PEER_ALLOW, NET_PEER_WAIT, NET_PEER_REFUSE };
NetPeerVerdict net_peer_verdict(const NetIdentity &claimed,
                                const NetIdentity &attested);

// This player now plays with this peer: the host's admission check let them
// in (the WELCOME is going out, at the waiting room or the mid-game rejoin
// door), or a joining client is entering the host's game. Said once per
// admission, never for a peer net_peer_verdict allowed but the room then
// refused (the ban list, the anonymous-players setting). For a platform that
// records who was played with (the Xbox fork reports the player's recent
// players); `attested` is the worker's attestation as above. Main thread,
// must not block. The default does nothing but log.
void net_peer_admitted(const NetIdentity &attested);

#endif /* NET_POLICY_H */
