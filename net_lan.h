#ifndef NET_LAN_H
#define NET_LAN_H

// LAN discovery + pairing for offline/local co-op (NETPLAY.md "Future
// milestone — LAN play"). LAN is NOT a mode: the host's lobby always runs
// an Announce (UDP beacon + TCP blob listener) alongside the relay flow,
// and the joiner's CodeEntry screen runs a Browse that lists beaconing
// hosts. Pairing reuses the manual INVITE blob byte-for-byte — the TCP
// socket is just a courier where the clipboard can't reach (two
// machines). The WebRTC session then comes up on host candidates, no
// STUN/TURN/relay involved (see NetTransport::set_lan_only).
//
// Wire format:
//   Beacon (UDP, 1/sec, broadcast + loopback, port NEWTONIA_LAN_PORT or
//   42607): "NWLN" u8 ver(1) u16 proto u16 tcp_port u8 name_len name.
//   Blob exchange (TCP to the beaconed port): u32 len + offer blob from
//   the host, then u32 len + answer blob back. One joiner at a time.
//
// Everything is single-threaded and non-blocking, driven by update()
// from the lobby tick. Desktop native only for now (NEWTONIA_LAN in the
// .cpp): browsers have no UDP, Android needs a MulticastLock, and iOS
// raw broadcast needs an Apple-gated entitlement (Bonjour later — the
// mobile phase gets its own discovery backends behind this same seam).

#include <string>
#include <vector>

#include <stdint.h>

namespace NetLan {

// False on platforms without a discovery backend (web/mobile/console):
// Announce/Browse construct fine but never find or serve anything.
bool available();

// The name this machine beacons as ("GLENN-MBP"); "NEWTONIA" fallback.
std::string local_host_name();

struct HostInfo {
  std::string name;   // beaconing machine's hostname (truncated)
  uint16_t proto;     // its PROTO_VERSION — mismatches draw greyed
  uint32_t addr;      // IPv4, network byte order (from recvfrom)
  uint16_t tcp_port;  // its blob-exchange listener
  int age_ms;         // since the last beacon; rows fade past ~2 missed
};

// Host side: beacon + serve the offer blob, collect the answer blob.
class Announce {
 public:
  Announce();
  ~Announce();
  bool start(const std::string &host_name);
  void stop();
  // The blob is minted asynchronously (ICE gathering); the beacon runs
  // from start() so joiners see the host immediately, and a connected
  // joiner simply waits until the blob exists.
  void set_offer_blob(const std::string &blob);
  // Drive everything; returns true exactly once when a joiner completed
  // the exchange (its answer blob in answer_out).
  bool update(int delta_ms, std::string &answer_out);
  bool running() const;
  // A joiner's TCP exchange is in flight (drawn as "PLAYER CONNECTING").
  bool peer_engaged() const;

 private:
  struct Impl;
  Impl *impl_;
};

// Joiner side: listen for beacons, then run one blob exchange.
class Browse {
 public:
  Browse();
  ~Browse();
  bool start();
  void stop();
  void update(int delta_ms);
  const std::vector<HostInfo> &hosts() const;
  // Begin the exchange with hosts()[index]. update() advances it.
  bool connect_host(int index);
  // True exactly once when the host's offer blob arrived.
  bool offer_ready(std::string &offer_out);
  void send_answer(const std::string &answer_blob);
  bool exchanging() const;  // between connect_host and completion/failure
  bool failed() const;      // the exchange died (refused/timeout/garbage)

 private:
  struct Impl;
  Impl *impl_;
};

}  // namespace NetLan

#endif /* NET_LAN_H */
