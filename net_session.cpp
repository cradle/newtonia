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
