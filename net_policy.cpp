#include "net_policy.h"

#include <cstdlib>
#include <cstring>

// Default backend: everything allowed, everywhere — shipping this seam
// changes no behavior on any platform. A platform backend TU defines
// NEWTONIA_NET_POLICY_BACKEND for the whole build and provides these
// functions itself (the Xbox fork's private privilege-check backend).

#ifndef NEWTONIA_NET_POLICY_BACKEND

bool net_online_play_allowed() { return true; }

void net_online_play_explain() {}

bool net_comms_allowed_with(const NetIdentity &) {
  // Test hook, inert without the env var (the pattern of the other
  // NEWTONIA_NET_TEST_* runtime hooks): refuse every peer, so the e2e
  // driver test/e2e/policy.sh can exercise the RejectNotAllowed handshake
  // path — the host's pre-WELCOME MSG_REJECT and the lobby's "CANNOT PLAY
  // WITH THAT PLAYER" screen — without a real platform policy backend.
  // Cached: this runs inside the handshake pump (see net_policy.h).
  static int refuse = -1;
  if (refuse < 0) {
    const char *e = std::getenv("NEWTONIA_NET_TEST_REFUSE_COMMS");
    refuse = (e && e[0] && e[0] != '0') ? 1 : 0;
  }
  return refuse == 0;
}

NetPeerVerdict net_peer_verdict(const NetIdentity &, const NetIdentity &) {
  // Test hook, inert without the env var: "refuse" refuses every peer and
  // "wait" never answers, so test/e2e/policy.sh can drive the held-WELCOME
  // path to both of its ends without a platform backend.
  static int mode = -1;
  if (mode < 0) {
    const char *e = std::getenv("NEWTONIA_NET_TEST_PEER_VERDICT");
    mode = !e ? 0 : std::strcmp(e, "refuse") == 0 ? 1
             : std::strcmp(e, "wait") == 0     ? 2 : 0;
  }
  return mode == 1 ? NET_PEER_REFUSE : mode == 2 ? NET_PEER_WAIT
                                                 : NET_PEER_ALLOW;
}

#endif  // NEWTONIA_NET_POLICY_BACKEND
