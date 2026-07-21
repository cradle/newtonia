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

  // Extra ICE servers (TURN) for this connection, as "urls\nuser\ncred"
  // triples from the signal relay. Must be called BEFORE start_host()/
  // start_join() — the peer connection is configured at creation. Default
  // no-op: backends without TURN support just stay STUN-only.
  virtual void set_ice_servers(const std::vector<std::string>& servers) {
    (void)servers;
  }

  // Relay-only ICE (TURN testing): same effect as the
  // NEWTONIA_NET_FORCE_RELAY=1 env var but reachable from touch builds —
  // the lobby arms it via a "0" typed before the room code (0 is not in
  // the code alphabet). One relay-forced side guarantees the whole pair
  // relays: it simply has no direct candidates to offer. Must be called
  // BEFORE start_host()/start_join(); default no-op.
  virtual void set_force_relay(bool on) { (void)on; }

  // LAN pairing (net_lan.h): configure NO ICE servers at all — host
  // candidates are the whole plan on a local network, and skipping STUN
  // means gathering completes immediately instead of waiting out an
  // (offline-unreachable) STUN query. Must be called BEFORE
  // start_host()/start_join(); default no-op.
  virtual void set_lan_only(bool on) { (void)on; }

  // Bytes queued in the transport's send buffers (both channels) that
  // have not reached the wire yet. Diagnosis telemetry: climbing during
  // an outage = OUR sender is blocked (the peer stopped acking — the
  // path or relay is eating the flow); staying ~0 while the peer logs a
  // gap = packets left this machine and are delayed in flight. Default
  // 0 for backends that can't tell.
  virtual int buffered_amount() const { return 0; }

  virtual void start_host() = 0;
  virtual void start_join(const std::string& remote_offer) = 0;

  // True once ICE gathering is complete and local_description() returns the
  // full SDP with all candidates embedded (non-trickle).
  virtual bool local_description_ready() const = 0;
  virtual std::string local_description() const = 0;
  virtual void set_remote_answer(const std::string& remote_answer) = 0;

  // ---- trickle ICE (M3-2b; room-code flow only) --------------------------
  // Call BEFORE start_host()/start_join(). In trickle mode
  // local_description_ready() flips as soon as the SDP exists (no
  // candidates embedded — no gathering wait); candidates stream out via
  // poll_local_candidate() and in via add_remote_candidate(), relayed as
  // {t:"cand"} frames by the signal worker. The manual clipboard flow
  // stays non-trickle (no live channel to carry candidates). Default
  // no-ops keep backends without trickle on the non-trickle path.
  virtual void set_trickle(bool on) { (void)on; }
  // Pops one gathered local candidate ("mid\ncandidate"); false = none.
  virtual bool poll_local_candidate(std::string& out) {
    (void)out;
    return false;
  }
  virtual void add_remote_candidate(const std::string& mid,
                                    const std::string& cand) {
    (void)mid;
    (void)cand;
  }

  virtual bool connected() const = 0;
  virtual bool failed() const = 0;

  // Diagnostics: the selected ICE path once connected, as
  // "local/remote" candidate types — "host/host" (direct), "srflx/..."
  // (NAT-punched), "relay/..." (TURN). Empty when unknown. Cheap enough
  // for the debug overlay to poll every frame.
  virtual std::string connection_info() const { return std::string(); }

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

// OS share sheet (M3-2): Android ACTION_SEND / iOS UIActivityViewController.
// No-ops (available()==false) everywhere else — the lobby falls back to the
// clipboard hint. Implemented in android_main.cpp / ios_main.mm.
void net_share_text(const std::string& text);
bool net_share_available();

// In-process loopback self-test: two transports host/join each other
// through the public API above and exchange messages on both channels.
// Returns true on PASS; false immediately where no backend exists. Wired
// to the NEWTONIA_NET_SELFTEST=1 environment hook in xbox_main.cpp and
// run by CI — blocks for up to ~90 s, never call it during gameplay.
bool net_selftest();

// Compile-time gate for netplay UI (the menu's ONLINE row) and the menu's
// invite-code intake. NEWTONIA_NET_DISABLED force-disables a platform whose
// backend would otherwise be compiled in — used by `make web NETPLAY=0` so
// the PUBLIC web deploys (GitHub Pages, itch release channel) ship with
// netplay off until live Worker/TURN usage is understood, while the
// netplay test channel keeps it on. The backend code still compiles (the
// lobby's clipboard helpers link from the same TUs); it is simply
// unreachable — nothing constructs a transport or signal socket, so the
// Worker and TURN are never contacted.
inline bool net_available() {
#if (defined(NEWTONIA_NET_RTC) || defined(__EMSCRIPTEN__)) && \
    !defined(NEWTONIA_NET_DISABLED)
  return true;
#else
  return false;
#endif
}

#endif /* NET_TRANSPORT_H */
