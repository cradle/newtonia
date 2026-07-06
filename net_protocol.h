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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace Net {

// Netplay diagnostics are opt-in via NEWTONIA_NET_DEBUG=1 (the e2e drivers
// set it): a shipped online session should not spew stdout — candidate
// bursts alone are dozens of lines a second. NET_LOG gates every net/lobby
// print behind one cached env check; user-facing text goes through Typer,
// never printf, so nothing here is ever needed in a release build.
inline bool net_debug_enabled() {
  static int on = -1;
  if (on < 0) { const char *e = std::getenv("NEWTONIA_NET_DEBUG"); on = (e && e[0]) ? 1 : 0; }
  return on != 0;
}

}  // namespace Net

#define NET_LOG(...) \
  do { if (::Net::net_debug_enabled()) { std::printf(__VA_ARGS__); std::fflush(stdout); } } while (0)

namespace Net {

// 2: per-ship warp count appended to the snapshot NetExtras ship record.
// 3: MSG_DELTA between 1 Hz keyframes (see NETPLAY.md M2-6).
// 4: client-authoritative aim — facing vector appended to MSG_INPUT.
// 5: move_flags (thrust/reverse/rotation) appended to the ship extras so
//    the peer can run the remote ship's exhaust-trail emitters.
// 6: mini-station bullet section appended after the per-ship extras
//    (its Save record carries no bullets — the client saw none).
// 7: gen-20 station enemies' bullet section appended after the
//    mini-station's, in enemies-list order.
const uint8_t PROTO_VERSION = 9;

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
  // host already rate-limits them to one per 125 ms. arg packs the
  // impact position as two int16s (x<<16 | y) so the client can spawn
  // the spark at the right spot.
  EV_ROID_THUD = 6,
  EV_ROID_TING = 7,
  // More host-simulated audio cues: the level-clear countdown tick
  // (1/s) and a pickup being collected.
  EV_LEVEL_TIC = 8,
  EV_PICKUP = 9,
  // Asteroid-vs-reflective-asteroid bounce; arg = volume 0..255 as the
  // host computed it (distance to the nearest player).
  EV_ROID_BOUNCE = 10,
  // A player ship bounced off (or was rammed by) an asteroid without
  // dying; arg = player index (1=host,2=client) | 0x100 when the
  // armoured-face ting applies.
  EV_SHIP_IMPACT = 11,
  // The host player fired their gun — the client plays it attenuated by
  // distance to its own ship (the client's shots are local, and the
  // host simulates the client's weapon itself).
  EV_REMOTE_SHOT = 12,
  // Host-simulated world actors (enemies, mini-station): a gunshot / a
  // ship-class explosion / the big station explosion, at a packed
  // position — the client plays them attenuated by listener distance.
  EV_WORLD_SHOT = 13,
  EV_WORLD_BOOM = 14,
  EV_STATION_BOOM = 15,
  // Friendly fire is a HOST preference but a room-wide rule: sent at
  // client bootstrap (join and rejoin) and on every host toggle; arg is
  // 0/1. The client adopts it for its HUD only — its own saved
  // preference is never touched.
  EV_FRIENDLY_FIRE = 16,
};

// Packs a world position into an event arg as two uint16 FRACTIONS of the
// world extent (0..65535 across 0..world_w/h). Raw int16 coordinates
// overflowed once the world grew past 32k (~generation 30, +3000/gen from
// gen 20); fractions always fit and the resolution (world/65535) stays
// sub-unit for small worlds and a few units for huge ones — fine for the
// audio-attenuation and impact-spark cues these positions drive. Both
// peers share the world size, so the fraction round-trips.
inline uint32_t pack_pos(float x, float y, float world_w, float world_h) {
  float fx = world_w > 0.0f ? x / world_w : 0.0f;
  float fy = world_h > 0.0f ? y / world_h : 0.0f;
  if (fx < 0.0f) fx = 0.0f; else if (fx > 1.0f) fx = 1.0f;
  if (fy < 0.0f) fy = 0.0f; else if (fy > 1.0f) fy = 1.0f;
  uint32_t ux = (uint32_t)(fx * 65535.0f + 0.5f);
  uint32_t uy = (uint32_t)(fy * 65535.0f + 0.5f);
  return (ux << 16) | uy;
}
inline void unpack_pos(uint32_t arg, float &x, float &y,
                       float world_w, float world_h) {
  x = (float)(arg >> 16)    / 65535.0f * world_w;
  y = (float)(arg & 0xffff) / 65535.0f * world_h;
}

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
