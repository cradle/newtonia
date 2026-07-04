#include "net_session.h"

#include <vector>

#include "net_protocol.h"
#include "savegame.h"

// ---- base64 -------------------------------------------------------------

namespace Net {

static const char B64_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const std::string &data) {
  std::string out;
  out.reserve(((data.size() + 2) / 3) * 4);
  size_t i = 0;
  while (i + 2 < data.size()) {
    uint32_t v = ((unsigned char)data[i] << 16) |
                 ((unsigned char)data[i + 1] << 8) | (unsigned char)data[i + 2];
    out += B64_CHARS[(v >> 18) & 63];
    out += B64_CHARS[(v >> 12) & 63];
    out += B64_CHARS[(v >> 6) & 63];
    out += B64_CHARS[v & 63];
    i += 3;
  }
  size_t rest = data.size() - i;
  if (rest == 1) {
    uint32_t v = (unsigned char)data[i] << 16;
    out += B64_CHARS[(v >> 18) & 63];
    out += B64_CHARS[(v >> 12) & 63];
    out += "==";
  } else if (rest == 2) {
    uint32_t v = ((unsigned char)data[i] << 16) |
                 ((unsigned char)data[i + 1] << 8);
    out += B64_CHARS[(v >> 18) & 63];
    out += B64_CHARS[(v >> 12) & 63];
    out += B64_CHARS[(v >> 6) & 63];
    out += '=';
  }
  return out;
}

static int b64_value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

bool base64_decode(const std::string &text, std::string &out) {
  out.clear();
  uint32_t acc = 0;
  int bits = 0;
  for (size_t i = 0; i < text.size(); i++) {
    char c = text[i];
    if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
    int v = b64_value(c);
    if (v < 0) return false;
    acc = (acc << 6) | (uint32_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += (char)((acc >> bits) & 0xff);
    }
  }
  return true;
}

// ---- signaling blobs ----------------------------------------------------
// "NWTN1O." + base64(sdp) for offers, "NWTN1A." for answers. The digit is
// the signaling format version, bumped independently of PROTO_VERSION.

static const char SIGNAL_PREFIX[] = "NWTN1";

std::string encode_signal(bool is_offer, const std::string &sdp) {
  std::string blob(SIGNAL_PREFIX);
  blob += is_offer ? 'O' : 'A';
  blob += '.';
  blob += base64_encode(sdp);
  return blob;
}

char decode_signal(const std::string &blob, std::string &sdp_out) {
  // Tolerate surrounding whitespace picked up by the clipboard.
  size_t begin = blob.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) return 0;
  size_t end = blob.find_last_not_of(" \t\r\n");
  std::string trimmed = blob.substr(begin, end - begin + 1);

  size_t prefix_len = sizeof(SIGNAL_PREFIX) - 1;
  if (trimmed.size() < prefix_len + 2) return 0;
  if (trimmed.compare(0, prefix_len, SIGNAL_PREFIX) != 0) return 0;
  char kind = trimmed[prefix_len];
  if ((kind != 'O' && kind != 'A') || trimmed[prefix_len + 1] != '.') return 0;
  if (!base64_decode(trimmed.substr(prefix_len + 2), sdp_out)) return 0;
  if (sdp_out.empty()) return 0;
  return kind;
}

std::string strip_ice_candidates(const std::string &sdp) {
  std::string out;
  out.reserve(sdp.size());
  size_t pos = 0;
  while (pos < sdp.size()) {
    size_t eol = sdp.find('\n', pos);
    size_t next = (eol == std::string::npos) ? sdp.size() : eol + 1;
    if (sdp.compare(pos, 12, "a=candidate:") != 0 &&
        sdp.compare(pos, 20, "a=end-of-candidates") != 0)
      out.append(sdp, pos, next - pos);
    pos = next;
  }
  return out;
}

// ---- INPUT messages -----------------------------------------------------

void encode_input(std::vector<uint8_t> &out, const InputState &in,
                  uint8_t player_id) {
  put_header(out, MSG_INPUT, player_id);
  put_u32(out, in.seq);
  put_u16(out, in.held);
  put_u8(out, in.boost_count);
  put_u8(out, in.next_weapon_count);
  put_u8(out, in.next_secondary_count);
  put_u8(out, in.teleport_count);
  put_u8(out, in.respawn_count);
  put_u8(out, in.shoot_press_count);
  put_u8(out, in.secondary_press_count);
  put_f32(out, in.analog_rotation);
  put_f32(out, in.analog_thrust);
  put_f32(out, in.analog_reverse);
}

bool decode_input(Reader &r, InputState &out) {
  out.seq = r.u32();
  out.held = r.u16();
  out.boost_count = r.u8();
  out.next_weapon_count = r.u8();
  out.next_secondary_count = r.u8();
  out.teleport_count = r.u8();
  out.respawn_count = r.u8();
  out.shoot_press_count = r.u8();
  out.secondary_press_count = r.u8();
  out.analog_rotation = r.f32();
  out.analog_thrust = r.f32();
  out.analog_reverse = r.f32();
  return r.ok;
}

// ---- snapshot chunking --------------------------------------------------

void send_snapshot(NetTransport *t, uint32_t snap_id,
                   const std::vector<uint8_t> &payload, uint8_t player_id) {
  size_t count = (payload.size() + SNAPSHOT_CHUNK_BYTES - 1) / SNAPSHOT_CHUNK_BYTES;
  if (count == 0) count = 1;  // an empty snapshot still sends one chunk
  for (size_t i = 0; i < count; i++) {
    size_t begin = i * SNAPSHOT_CHUNK_BYTES;
    size_t size = payload.size() - begin;
    if (size > SNAPSHOT_CHUNK_BYTES) size = SNAPSHOT_CHUNK_BYTES;
    std::vector<uint8_t> msg;
    msg.reserve(HEADER_SIZE + 8 + size);
    put_header(msg, MSG_SNAPSHOT_CHUNK, player_id);
    put_u32(msg, snap_id);
    put_u16(msg, (uint16_t)i);
    put_u16(msg, (uint16_t)count);
    if (size) put_bytes(msg, &payload[begin], size);
    t->send_reliable(&msg[0], msg.size());
  }
}

bool SnapshotAssembler::add_chunk(Reader &r) {
  uint32_t snap_id = r.u32();
  uint16_t index = r.u16();
  uint16_t count = r.u16();
  if (!r.ok || count == 0) return false;

  if (index == 0) {
    // Start of a snapshot; abandons any half-assembled predecessor.
    snap_id_ = snap_id;
    expect_index_ = 0;
    count_ = count;
    payload_.clear();
  } else if (snap_id != snap_id_ || index != expect_index_ ||
             count != count_) {
    // Not the chunk we were waiting for — drop the partial snapshot and
    // wait for the next index-0 chunk.
    count_ = 0;
    return false;
  }
  if (count_ == 0) return false;

  size_t n = r.remaining();
  if (n) {
    const uint8_t *bytes = r.bytes(n);
    payload_.insert(payload_.end(), bytes, bytes + n);
  }
  expect_index_++;
  if (expect_index_ < count_) return false;
  count_ = 0;  // complete — reset for the next snapshot
  return true;
}

}  // namespace Net

// ---- handshake ----------------------------------------------------------

namespace {

// Milestone 1 has no build-fingerprint check (a per-build hash would break
// native<->web cross-play); the field is reserved on the wire.
const uint32_t BUILD_ID = 0;

// Connected transport but no valid HELLO/WELCOME inside this window means
// the peer is not a compatible game — give up rather than sit forever.
const int HANDSHAKE_TIMEOUT_MS = 10000;

void send_hello(NetTransport *t) {
  std::vector<uint8_t> msg;
  Net::put_header(msg, Net::MSG_HELLO, 2);
  Net::put_u16(msg, Save::GameState::VERSION);
  Net::put_u32(msg, BUILD_ID);
  t->send_reliable(&msg[0], msg.size());
}

void send_welcome(NetTransport *t) {
  std::vector<uint8_t> msg;
  Net::put_header(msg, Net::MSG_WELCOME, 1);
  Net::put_u8(msg, 2);     // assigned player id
  Net::put_u16(msg, 8);    // step_size (ms) — informational for now
  Net::put_u16(msg, 100);  // snapshot period (ms)
  t->send_reliable(&msg[0], msg.size());
}

void send_reject(NetTransport *t, uint8_t reason) {
  std::vector<uint8_t> msg;
  Net::put_header(msg, Net::MSG_REJECT, 1);
  Net::put_u8(msg, reason);
  t->send_reliable(&msg[0], msg.size());
}

}  // namespace

NetSession::NetSession(NetTransport *transport, Role role)
    : transport_(transport),
      role_(role),
      phase_(Connecting),
      reject_reason_(0),
      hello_sent_(false),
      handshake_ms_(0) {}

NetSession::~NetSession() {
  if (transport_) {
    transport_->close();
    delete transport_;
  }
}

void NetSession::update(int delta_ms) {
  if (phase_ == Ready || phase_ == Rejected || phase_ == Failed) return;

  if (transport_->failed()) {
    fail();
    return;
  }

  if (phase_ == Connecting) {
    if (!transport_->connected()) return;
    phase_ = Handshaking;
    if (role_ == ClientRole && !hello_sent_) {
      send_hello(transport_);
      hello_sent_ = true;
    }
  }

  handshake_ms_ += delta_ms;
  if (handshake_ms_ > HANDSHAKE_TIMEOUT_MS) {
    fail();
    return;
  }

  std::vector<unsigned char> msg;
  while (transport_->poll(msg)) {
    Net::Reader r(msg.empty() ? nullptr : &msg[0], msg.size());
    Net::Header h;
    if (!Net::read_header(r, h)) continue;  // foreign protocol version: skip

    if (role_ == HostRole && h.msg_type == Net::MSG_HELLO) {
      uint16_t save_version = r.u16();
      (void)r.u32();  // build id — reserved
      if (!r.ok) continue;
      if (save_version != Save::GameState::VERSION) {
        send_reject(transport_, RejectVersionMismatch);
        reject_reason_ = RejectVersionMismatch;
        phase_ = Rejected;
        return;
      }
      send_welcome(transport_);
      phase_ = Ready;
      return;
    }

    if (role_ == ClientRole && h.msg_type == Net::MSG_WELCOME) {
      uint8_t assigned = r.u8();
      (void)r.u16();  // step size
      (void)r.u16();  // snapshot period
      if (!r.ok || assigned != 2) continue;
      phase_ = Ready;
      return;
    }

    if (role_ == ClientRole && h.msg_type == Net::MSG_REJECT) {
      reject_reason_ = r.u8();
      phase_ = Rejected;
      return;
    }
  }
}
