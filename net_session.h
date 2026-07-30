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
#include <vector>

#include "net_identity.h"
#include "net_protocol.h"
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

// Removes a=candidate / a=end-of-candidates lines. The joiner applies this
// to the pasted offer: with no candidate pairs to check, its ICE agent has
// nothing to time out on while the humans ferry the reply code back (which
// can take minutes — far past ICE's ~10 s give-up). The host gets the
// joiner's full candidates in the reply and initiates every connectivity
// check the moment it is pasted; the joiner then answers via peer-reflexive
// discovery, so the failure clock only starts once both sides are ready.
std::string strip_ice_candidates(const std::string &sdp);

// ---- INPUT messages -----------------------------------------------------
// Client -> host, unreliable, one per 8 ms tick. Held actions travel as a
// bitmask; one-shot actions as wrapping counters so a lost packet can
// neither drop nor double-fire them (the host applies the counter delta).

struct InputState {
  uint32_t seq;            // host ignores stale sequence numbers
  uint16_t held;           // InputBit mask
  uint8_t boost_count;     // wrapping one-shot counters
  uint8_t next_weapon_count;
  uint8_t next_secondary_count;
  uint8_t teleport_count;
  uint8_t respawn_count;   // respawn-tap while dead
  uint8_t shoot_press_count;      // fire-key presses (semi-auto edges)
  uint8_t secondary_press_count;  // secondary-key presses
  float analog_rotation;   // rotation_scale (0..1; 1 = keyboard)
  float analog_thrust;     // thrust_analog
  float analog_reverse;    // reverse_analog
  // Client-authoritative aim (v4): the exact facing the client's screen
  // shows — rotation inertia, analog rate and sensitivity included. The
  // rotate held-bits only extrapolate between INPUT arrivals. (0,0) =
  // no aim data; the host ignores it.
  float facing_x;
  float facing_y;
  // Client-authoritative pose (v12, same rationale as the aim): the exact
  // position/velocity the client's screen shows. The host adopts it — the
  // pilot is never rubberbanded by corrections, and host-side collisions
  // match what the player actually saw — once warp_echo proves the client
  // has seen the latest host-driven respawn/teleport (Ship::net_warp_count;
  // in-flight INPUTs from before a warp would drag the ship straight back).
  float pos_x, pos_y;
  float vel_x, vel_y;
  uint8_t warp_echo;

  InputState()
      : seq(0), held(0), boost_count(0), next_weapon_count(0),
        next_secondary_count(0), teleport_count(0), respawn_count(0),
        shoot_press_count(0), secondary_press_count(0),
        analog_rotation(1.0f), analog_thrust(1.0f), analog_reverse(1.0f),
        facing_x(0.0f), facing_y(0.0f),
        pos_x(0.0f), pos_y(0.0f), vel_x(0.0f), vel_y(0.0f), warp_echo(0) {}
};

// Appends the complete MSG_INPUT message (header included).
void encode_input(std::vector<uint8_t> &out, const InputState &in,
                  uint8_t player_id);
// Reads the body after the header; false on short/corrupt message.
bool decode_input(Reader &r, InputState &out);

// ---- snapshot chunking --------------------------------------------------
// SNAPSHOT_CHUNK body: u32 snap_id | u16 index | u16 count | bytes.
// 15 KB chunks stay comfortably under browser DataChannel message limits.

const size_t SNAPSHOT_CHUNK_BYTES = 15 * 1024;

// Upper bound on a reassembled snapshot. Real snapshots run well under 1 MB;
// this caps the memory a malicious host can force the client to allocate
// (up to 65535 chunks would otherwise be ~1 GB).
const size_t SNAPSHOT_MAX_BYTES = 4 * 1024 * 1024;

// Splits payload into chunks and sends them on the reliable channel.
void send_snapshot(NetTransport *t, uint32_t snap_id,
                   const std::vector<uint8_t> &payload, uint8_t player_id);

// Client-side reassembly. Chunks arrive in order (reliable channel); a new
// snap_id abandons any half-built predecessor.
class SnapshotAssembler {
 public:
  SnapshotAssembler() : snap_id_(0), expect_index_(0), count_(0) {}

  // Feed one SNAPSHOT_CHUNK body (reader positioned after the header).
  // Returns true when this chunk completed a snapshot; fetch it with
  // take_payload() before feeding more.
  bool add_chunk(Reader &r);

  uint32_t snap_id() const { return snap_id_; }
  std::vector<uint8_t> &payload() { return payload_; }

 private:
  uint32_t snap_id_;
  uint16_t expect_index_;
  uint16_t count_;
  std::vector<uint8_t> payload_;
};

}  // namespace Net

namespace Save { struct GameState; }

// Sanity gate for a GameState deserialized from a host snapshot. The
// deserializer trusts its input (it was written for local save files), so
// the network paths screen the result here before applying it: a malicious
// host could otherwise send absurd world dimensions (Grid allocation blowup)
// or oversized entity vectors. Returns false when any field is out of the
// range a genuine snapshot stays within — including NaN/Inf and absurd
// magnitudes in every position/velocity, which no range comparison catches
// on its own (see the definition).
bool net_state_sane(const Save::GameState &s);

// Per-float screens shared by net_state_sane and the projectile parse. The
// limits are far outside anything legal (the world caps at 200000 units and
// wire pose checks use ~3 units/ms) — they exist to keep a NaN or an absurd
// magnitude out of WrappedPoint, Object::radius and the collision grid,
// whose out-of-range cell normalization is a per-index loop.
static const float NET_COORD_LIMIT = 1.0e7f;
static const float NET_VEL_LIMIT   = 1.0e4f;
bool net_coord_sane(float v);
bool net_vel_sane(float v);

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

  // Wire values in MSG_REJECT's reason byte — append only. An old build
  // receiving a value it doesn't know renders its generic "HOST REFUSED
  // THE CONNECTION" text, so new reasons are safe to add.
  enum RejectReason {
    RejectVersionMismatch = 1,
    // Platform policy refused this pairing (net_policy.h,
    // net_comms_allowed_with — checked inside the handshake, before the
    // host's WELCOME, so a blocked joiner gets an honest refusal instead
    // of a ghosted CONNECTED screen). Also set locally (no wire message)
    // when the CLIENT's own policy refuses the host's identity.
    RejectNotAllowed = 2,
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

  // The peer's identity as parsed from its HELLO (host role) / WELCOME
  // (client role) append — see net_protocol.h. Default-constructed (platform
  // Unknown, empty name) for a legacy peer or before Ready. A rejoin runs a
  // fresh NetSession, so the identity re-arrives with every handshake.
  const NetIdentity &peer_identity() const { return peer_identity_; }

  NetTransport *transport() { return transport_; }

private:
  void fail() { phase_ = Failed; }

  NetTransport *transport_;
  Role role_;
  Phase phase_;
  uint8_t reject_reason_;
  bool hello_sent_;
  int handshake_ms_;  // time spent in Handshaking, for the timeout
  NetIdentity peer_identity_;
};

#endif /* NET_SESSION_H */
