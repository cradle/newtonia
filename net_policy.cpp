#include "net_policy.h"
#include "net_protocol.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

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

NetPeerVerdict net_peer_verdict(const NetIdentity &claimed,
                                const NetIdentity &attested) {
  // Test hook, inert without the env var. "refuse" refuses every peer and
  // "wait" never answers, so test/e2e/policy_peer.sh can drive the held
  // WELCOME and the joiner's gate to both of their ends without a platform
  // backend. "rejoin-wait" allows the first episode and never answers
  // later ones; "rejoin-wait:N" answers a later episode after N ms
  // (test/e2e/policy_peer_rejoin.sh: the paused-host reconnect). An
  // episode is a run of calls without a second's gap, which is how the
  // joiner's per-tick poll looks while it holds a bootstrap.
  // "refuse-name:NAME[,NAME...]" refuses only the peers with those names,
  // claimed or attested, standing in for players on a block list
  // (test/e2e/policy_members.sh: other joiners in a 3-4 player room).
  // "slow:N" answers nothing for N ms after its first call and allows
  // everyone after: a joiner whose own platform check is slow
  // (policy_members.sh: the rejoin door holds them meanwhile).
  // "slow-each:N" is the same per peer: each name answers N ms after it
  // was first asked about, so one player's checks of several others start
  // and finish separately (door_hold_queue.sh).
  static int mode = -1;
  static long wait_ms = -1;
  static std::string refused_name;
  if (mode < 0) {
    const char *e = std::getenv("NEWTONIA_NET_TEST_PEER_VERDICT");
    mode = !e ? 0 : std::strcmp(e, "refuse") == 0 ? 1
             : std::strcmp(e, "wait") == 0       ? 2
             : std::strncmp(e, "rejoin-wait", 11) == 0 ? 3
             : std::strncmp(e, "refuse-name:", 12) == 0 ? 4
             : std::strncmp(e, "slow:", 5) == 0           ? 5
             : std::strncmp(e, "slow-each:", 10) == 0     ? 6 : 0;
    if (mode == 3 && e[11] == ':') wait_ms = std::atol(e + 12);
    if (mode == 4) refused_name = e + 12;
    if (mode == 5) wait_ms = std::atol(e + 5);
    if (mode == 6) wait_ms = std::atol(e + 10);
  }
  if (mode == 6) {
    static std::map<std::string, std::chrono::steady_clock::time_point>
        first_asked;
    const std::string who = !attested.name.empty() ? attested.name
                                                   : claimed.name;
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    std::map<std::string, std::chrono::steady_clock::time_point>::iterator it =
        first_asked.find(who);
    if (it == first_asked.end())
      it = first_asked.insert(std::make_pair(who, now)).first;
    return now - it->second >= std::chrono::milliseconds(wait_ms)
               ? NET_PEER_ALLOW
               : NET_PEER_WAIT;
  }
  if (mode == 5) {
    static bool started = false;
    static std::chrono::steady_clock::time_point first;
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (!started) {
      started = true;
      first = now;
    }
    return now - first >= std::chrono::milliseconds(wait_ms) ? NET_PEER_ALLOW
                                                             : NET_PEER_WAIT;
  }
  if (mode == 4) {
    const std::string list = "," + refused_name + ",";
    const bool hit =
        (!claimed.name.empty() &&
         list.find("," + claimed.name + ",") != std::string::npos) ||
        (!attested.name.empty() &&
         list.find("," + attested.name + ",") != std::string::npos);
    return hit ? NET_PEER_REFUSE : NET_PEER_ALLOW;
  }
  if (mode == 1) return NET_PEER_REFUSE;
  if (mode == 2) return NET_PEER_WAIT;
  if (mode != 3) return NET_PEER_ALLOW;
  typedef std::chrono::steady_clock clock;
  static bool any = false;
  static int episode = 0;
  static clock::time_point last, start;
  clock::time_point now = clock::now();
  if (!any || now - last > std::chrono::milliseconds(1000)) {
    ++episode;
    start = now;
  }
  any = true;
  last = now;
  if (episode == 1) return NET_PEER_ALLOW;
  if (wait_ms >= 0 && now - start >= std::chrono::milliseconds(wait_ms))
    return NET_PEER_ALLOW;
  return NET_PEER_WAIT;
}

void net_peer_admitted(const NetIdentity &) {
  // The e2e drivers count this line (test/e2e/nseat_anon.sh): one per
  // admitted pilot, none for a refused one.
  NET_LOG("net: platform told peer admitted\n");
}

#endif  // NEWTONIA_NET_POLICY_BACKEND
