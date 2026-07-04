#ifndef NET_SESSION_H
#define NET_SESSION_H

// Netplay session layer — see NETPLAY.md.
//
// Sits between the raw NetTransport and the game: signaling-blob
// encoding for the clipboard copy-paste flow, and the HELLO/WELCOME
// handshake that runs as soon as the transport connects. Snapshot
// chunking/reassembly joins this file in Phase 6.

#include <stdint.h>

#include <string>

#include "net_transport.h"

namespace Net {

// ---- signaling blobs ----------------------------------------------------
// What actually travels through the clipboard: a direction prefix plus the
// base64 SDP. The prefix catches wrong-direction pastes (host pasting an
// offer, joiner pasting an answer) with a clear message instead of an
// opaque connection failure.

std::string base64_encode(const std::string &data);
bool base64_decode(const std::string &text, std::string &out);

std::string encode_signal(bool is_offer, const std::string &sdp);

// Returns 'O' (offer) or 'A' (answer) with the SDP in sdp_out, or 0 when
// the blob isn't ours / is corrupted.
char decode_signal(const std::string &blob, std::string &sdp_out);

}  // namespace Net

// ---- session ------------------------------------------------------------
// Owns the transport once signaling hands it over. update() pumps the
// transport each tick and drives the handshake:
//   client: on connect, send HELLO (save-format version + build id)
//   host:   validate HELLO -> WELCOME (player id, step size, snapshot
//           period) or REJECT (reason)
// Once Ready, the session keeps ownership of the transport for the game
// phases; messages that are not handshake traffic are left queued for
// whoever polls the transport next.

class NetSession {
public:
  enum Role { HostRole, ClientRole };
  enum Phase {
    Connecting,   // transport still establishing
    Handshaking,  // connected, HELLO/WELCOME in flight
    Ready,        // handshake complete — game can start
    Rejected,     // host refused (version mismatch); reject_reason() set
    Failed,       // transport failed or handshake timed out
  };

  enum RejectReason {
    RejectVersionMismatch = 1,
  };

  // Takes ownership of the transport (must be non-null).
  NetSession(NetTransport *transport, Role role);
  ~NetSession();

  void update(int delta_ms);

  Phase phase() const { return phase_; }
  Role role() const { return role_; }
  // 1 = host, 2 = client. Valid from construction (fixed by role in M1).
  int player_id() const { return role_ == HostRole ? 1 : 2; }
  uint8_t reject_reason() const { return reject_reason_; }

  NetTransport *transport() { return transport_; }

private:
  void fail() { phase_ = Failed; }

  NetTransport *transport_;
  Role role_;
  Phase phase_;
  uint8_t reject_reason_;
  bool hello_sent_;
  int handshake_ms_;  // time spent in Handshaking, for the timeout
};

#endif /* NET_SESSION_H */
