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

// Clipboard for the lobby's copy-paste signaling, uniform across the sync
// native path (SDL clipboard: read_start is a no-op, read_poll completes
// immediately) and the async web path (navigator.clipboard: start the
// read inside a key-press gesture or the browser denies it, then poll
// each frame). read_poll returns false while pending; on completion it
// returns true with the text in out (empty = denied/nothing to paste).
void net_clipboard_write(const std::string& text);
void net_clipboard_read_start();
bool net_clipboard_read_poll(std::string& out);

// In-process loopback self-test: two transports host/join each other
// through the public API above and exchange messages on both channels.
// Returns true on PASS; false immediately where no backend exists. Wired
// to the NEWTONIA_NET_SELFTEST=1 environment hook in xbox_main.cpp and
// run by CI — blocks for up to ~90 s, never call it during gameplay.
bool net_selftest();

// Compile-time gate for netplay UI (the menu's ONLINE row).
inline bool net_available() {
#if defined(NEWTONIA_NET_RTC) || defined(__EMSCRIPTEN__)
  return true;
#else
  return false;
#endif
}

#endif /* NET_TRANSPORT_H */
