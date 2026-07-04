#ifndef NET_TRANSPORT_H
#define NET_TRANSPORT_H

// Netplay transport seam — see NETPLAY.md.
//
// Abstract peer-to-peer transport over WebRTC DataChannels. Two channels:
// "rel" (ordered/reliable: handshake, snapshots, events) and "unrel"
// (unordered, maxRetransmits=0: per-tick input). Signaling in Milestone 1
// is manual copy-paste of the full non-trickle SDP, so the flow is:
//
//   host:   start_host()  -> local_description_ready() -> local_description()
//           (send offer to friend) -> set_remote_answer(pasted answer)
//   joiner: start_join(pasted offer) -> local_description_ready()
//           -> local_description() (send answer back)
//   both:   connected() flips true, then send_*/poll() until close().
//
// Backends: native libdatachannel (net_transport_rtc.cpp, compiled only
// with NEWTONIA_NET_RTC), browser RTCPeerConnection (net_transport_web.cpp,
// compiled only under __EMSCRIPTEN__). Everywhere else create() returns
// nullptr and the menu never shows ONLINE.

#include <cstddef>
#include <string>
#include <vector>

class NetTransport {
public:
  virtual ~NetTransport() {}

  virtual void start_host() = 0;
  virtual void start_join(const std::string& remote_offer) = 0;

  // True once ICE gathering is complete and local_description() returns the
  // full SDP with all candidates embedded (non-trickle).
  virtual bool local_description_ready() const = 0;
  virtual std::string local_description() const = 0;
  virtual void set_remote_answer(const std::string& remote_answer) = 0;

  virtual bool connected() const = 0;
  virtual bool failed() const = 0;

  virtual void send_reliable(const void* data, size_t size) = 0;
  virtual void send_unreliable(const void* data, size_t size) = 0;

  // Pops one whole inbound message into out (main thread only); returns
  // false when nothing is pending.
  virtual bool poll(std::vector<unsigned char>& out) = 0;

  virtual void close() = 0;

  // Platform backend, or nullptr where netplay is unavailable. Caller owns
  // the result.
  static NetTransport* create();
};

// Compile-time gate for netplay UI (the menu's ONLINE row).
inline bool net_available() {
#if defined(NEWTONIA_NET_RTC) || defined(__EMSCRIPTEN__)
  return true;
#else
  return false;
#endif
}

#endif /* NET_TRANSPORT_H */
