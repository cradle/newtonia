#ifndef NET_SIGNAL_H
#define NET_SIGNAL_H

// Netplay signaling seam — see NETPLAY.md (Milestone 2).
//
// Thin WebSocket client for the room-code flow against the signal worker
// (signal/): the host connects and is assigned a 4-letter room code; the
// joiner connects with that code; the worker relays the non-trickle
// offer/answer between them. Replaces the Milestone 1 clipboard ferry as
// the primary path (the clipboard flow stays as a fallback).
//
//   host:   connect_host(url) -> Room{code} -> send_offer(sdp)
//           -> Answer{sdp} (PeerJoin/PeerLeave arrive as they happen)
//   joiner: connect_join(url, code) -> Joined -> Offer{sdp}
//           -> send_answer(sdp)
//
// Backends mirror the transport seam: native = libdatachannel's built-in
// WebSocket (net_signal_rtc.cpp, NEWTONIA_NET_RTC), web = browser
// WebSocket (net_signal_web.cpp, __EMSCRIPTEN__); create() returns
// nullptr everywhere else. Callbacks enqueue raw frames; poll() parses on
// the main thread.

#include <string>

class NetSignal {
public:
  struct Event {
    enum Kind {
      Room,       // text = room code, text2 = reclaim token (host)
      Joined,     // join accepted (joiner)
      Offer,      // text = offer SDP (joiner)
      Answer,     // text = answer SDP (host)
      PeerJoin,   // a joiner entered the room (host)
      PeerLeave,  // the joiner left the room (host)
      Ice,        // text = "urls\nusername\ncredential" (one per server)
      Cand,       // trickle ICE: text = candidate line, text2 = mid
      Error,      // text = reason ("no-such-room", "room-full", "expired")
      Closed,     // socket closed / connect failed
    };
    Kind kind;
    std::string text;
    std::string text2;  // secondary field (Room: the reclaim token)
  };

  virtual ~NetSignal() {}

  virtual void connect_host(const std::string &url) = 0;
  // M3-1 host reclaim: reattach to an existing room after a signal drop,
  // proving ownership with the token from the original Room frame. The
  // worker replies with the same Room frame; the host then re-offers.
  virtual void connect_host_reclaim(const std::string &url,
                                    const std::string &code,
                                    const std::string &token) = 0;
  virtual void connect_join(const std::string &url,
                            const std::string &code) = 0;

  virtual void send_offer(const std::string &sdp) = 0;
  virtual void send_answer(const std::string &sdp) = 0;
  // Trickle ICE (M3-2b): one gathered candidate, relayed to the peer.
  virtual void send_cand(const std::string &mid, const std::string &cand) = 0;

  // Pops one event (main thread only); false when nothing is pending.
  virtual bool poll(Event &ev) = 0;

  virtual void close() = 0;

  // Platform backend, or nullptr where netplay is unavailable. Caller
  // owns the result.
  static NetSignal *create();
};

// Signal server URL: NEWTONIA_SIGNAL_URL env var when set, otherwise the
// baked-in production worker (placeholder until M2-3 deploys it).
std::string net_signal_url();

// The room code alphabet (shared with signal/src/worker.js): 5 chars,
// no 0/O/1/I/5/S (confusable in the game font) or F (fullscreen key).
// Used by the lobby's code-entry screen.
bool net_room_code_char_ok(char c);
const int NET_ROOM_CODE_LEN = 5;

// ---- tiny JSON helpers (shared by both backends) ------------------------
// Only what the signal protocol needs: escape a string value for
// embedding, and pull one string field out of a flat JSON object frame.
namespace NetSig {
std::string json_escape(const std::string &s);
// Extracts "key":"value" from a one-level object; false if absent.
bool json_field(const std::string &json, const char *key, std::string &out);
// Parses one raw frame from the worker into an Event; false = unknown.
bool parse_frame(const std::string &frame, NetSignal::Event &ev);
}  // namespace NetSig

// In-process self-test against a live relay (wrangler dev or production):
// host+join sockets, code round-trip, offer/answer relay. Blocks up to
// ~20 s. Wired to NEWTONIA_SIGNAL_SELFTEST=1 (uses net_signal_url()).
bool net_signal_selftest();

#endif /* NET_SIGNAL_H */
