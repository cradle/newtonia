#ifndef NET_PROTOCOL_H
#define NET_PROTOCOL_H

// Netplay wire protocol — see NETPLAY.md ("Protocol quick-ref").
//
// Every message starts with a 4-byte header:
//   uint8 proto_version (= PROTO_VERSION)
//   uint8 msg_type      (Net::MsgType)
//   uint8 player_id
//   uint8 reserved      (0)
// All multi-byte fields are little-endian and packed/unpacked explicitly
// byte-by-byte — structs are never memcpy'd onto the wire (native and web
// builds must interoperate regardless of compiler padding).

#include <stdint.h>

#include <cstddef>
#include <cstring>
#include <vector>

namespace Net {

// 2: per-ship warp count appended to the snapshot NetExtras ship record.
// 3: MSG_DELTA between 1 Hz keyframes (see NETPLAY.md M2-6).
// 4: client-authoritative aim — facing vector appended to MSG_INPUT.
// 5: move_flags (thrust/reverse/rotation) appended to the ship extras so
//    the peer can run the remote ship's exhaust-trail emitters.
const uint8_t PROTO_VERSION = 5;

enum MsgType {
  MSG_HELLO = 1,           // C->H rel: proto + save version + build check
  MSG_WELCOME = 2,         // H->C rel: assigned player_id, timing constants
  MSG_REJECT = 3,          // H->C rel: version/build mismatch, lobby full
  MSG_INPUT = 4,           // C->H unrel: per-tick input state
  MSG_SNAPSHOT_CHUNK = 5,  // H->C rel: chunked KEYFRAME snapshot, 1 Hz
  MSG_DELTA = 7,           // H->C rel: between-keyframe delta, 10 Hz
  MSG_EVENT = 6,           // both ways, rel: EventCode below
};

enum EventCode {
  EV_PAUSE = 1,
  EV_RESUME = 2,
  EV_GENERATION_START = 3,  // + uint32 generation
  EV_GAME_OVER = 4,
  EV_BYE = 5,
  // Host->client impact cues the client can't simulate (bullet hits on
  // invincible/tough asteroids and reflective/phasing deflections). The
  // host already rate-limits them to one per 125 ms.
  EV_ROID_THUD = 6,
  EV_ROID_TING = 7,
};

// MSG_INPUT held-button bitmask (uint16). One-shot actions (boost,
// next_weapon, next_secondary, teleport, respawn tap) travel as wrapping
// uint8 counters instead, so a lost unreliable packet can neither drop nor
// double-fire them.
enum InputBit {
  IN_LEFT = 1 << 0,
  IN_RIGHT = 1 << 1,
  IN_THRUST = 1 << 2,
  IN_REVERSE = 1 << 3,
  IN_SHOOT = 1 << 4,
  IN_SECONDARY = 1 << 5,
};

struct Header {
  uint8_t proto_version;
  uint8_t msg_type;
  uint8_t player_id;
  uint8_t reserved;
};

const size_t HEADER_SIZE = 4;

// ---- encoding (append to a byte buffer) --------------------------------

inline void put_u8(std::vector<uint8_t>& buf, uint8_t v) {
  buf.push_back(v);
}

inline void put_u16(std::vector<uint8_t>& buf, uint16_t v) {
  buf.push_back((uint8_t)(v & 0xff));
  buf.push_back((uint8_t)((v >> 8) & 0xff));
}

inline void put_u32(std::vector<uint8_t>& buf, uint32_t v) {
  buf.push_back((uint8_t)(v & 0xff));
  buf.push_back((uint8_t)((v >> 8) & 0xff));
  buf.push_back((uint8_t)((v >> 16) & 0xff));
  buf.push_back((uint8_t)((v >> 24) & 0xff));
}

inline void put_f32(std::vector<uint8_t>& buf, float v) {
  uint32_t bits;
  std::memcpy(&bits, &v, sizeof(bits));  // IEEE-754 bit pattern, not a struct
  put_u32(buf, bits);
}

inline void put_bytes(std::vector<uint8_t>& buf, const void* data,
                      size_t size) {
  const uint8_t* p = (const uint8_t*)data;
  buf.insert(buf.end(), p, p + size);
}

inline void put_header(std::vector<uint8_t>& buf, uint8_t msg_type,
                       uint8_t player_id) {
  put_u8(buf, PROTO_VERSION);
  put_u8(buf, msg_type);
  put_u8(buf, player_id);
  put_u8(buf, 0);
}

// ---- decoding (bounds-checked cursor over a received buffer) -----------
//
// On under-run every read returns 0 and ok flips false; callers check ok
// once at the end instead of guarding each field.

struct Reader {
  const uint8_t* data;
  size_t size;
  size_t pos;
  bool ok;

  Reader(const uint8_t* data_, size_t size_)
      : data(data_), size(size_), pos(0), ok(true) {}
  Reader(const std::vector<uint8_t>& buf)
      : data(buf.empty() ? nullptr : &buf[0]),
        size(buf.size()),
        pos(0),
        ok(true) {}

  bool has(size_t n) {
    if (pos + n > size) ok = false;
    return ok;
  }

  uint8_t u8() {
    if (!has(1)) return 0;
    return data[pos++];
  }

  uint16_t u16() {
    if (!has(2)) return 0;
    uint16_t v = (uint16_t)(data[pos] | (data[pos + 1] << 8));
    pos += 2;
    return v;
  }

  uint32_t u32() {
    if (!has(4)) return 0;
    uint32_t v = (uint32_t)data[pos] | ((uint32_t)data[pos + 1] << 8) |
                 ((uint32_t)data[pos + 2] << 16) |
                 ((uint32_t)data[pos + 3] << 24);
    pos += 4;
    return v;
  }

  float f32() {
    uint32_t bits = u32();
    float v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
  }

  const uint8_t* bytes(size_t n) {
    if (!has(n)) return nullptr;
    const uint8_t* p = data + pos;
    pos += n;
    return p;
  }

  size_t remaining() const { return ok ? size - pos : 0; }
};

// Reads and validates the common header; returns false (and leaves the
// reader failed) on short buffers or a foreign protocol version.
inline bool read_header(Reader& r, Header& h) {
  h.proto_version = r.u8();
  h.msg_type = r.u8();
  h.player_id = r.u8();
  h.reserved = r.u8();
  if (!r.ok || h.proto_version != PROTO_VERSION) {
    r.ok = false;
    return false;
  }
  return true;
}

}  // namespace Net

#endif /* NET_PROTOCOL_H */
