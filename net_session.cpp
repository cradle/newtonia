#include "net_session.h"

#include <SDL.h>

#include <cmath>
#include <cstdlib>
#include <vector>

#include "net_policy.h"
#include "net_protocol.h"
#include "hazard.h"       // Hazard::HUNTER (net_state_sane's kind bound)
#include "preferences.h"  // MAX_PLAYERS (net_state_sane's seat bound)
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
  put_f32(out, in.facing_x);
  put_f32(out, in.facing_y);
  put_f32(out, in.pos_x);
  put_f32(out, in.pos_y);
  put_f32(out, in.vel_x);
  put_f32(out, in.vel_y);
  put_u8(out, in.warp_echo);
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
  out.facing_x = r.f32();
  out.facing_y = r.f32();
  out.pos_x = r.f32();
  out.pos_y = r.f32();
  out.vel_x = r.f32();
  out.vel_y = r.f32();
  out.warp_echo = r.u8();
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
    // Reject up front any count that would let the host force an oversized
    // allocation — genuine snapshots are far under SNAPSHOT_MAX_BYTES.
    if ((size_t)count * SNAPSHOT_CHUNK_BYTES > SNAPSHOT_MAX_BYTES) {
      count_ = 0;
      return false;
    }
    snap_id_ = snap_id;
    expect_index_ = 0;
    count_ = count;
    payload_.clear();
    payload_.reserve((size_t)count * SNAPSHOT_CHUNK_BYTES);
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

// ---- deserialized-state sanity gate -------------------------------------

bool net_coord_sane(float v) {
  return std::isfinite(v) && v > -NET_COORD_LIMIT && v < NET_COORD_LIMIT;
}

bool net_vel_sane(float v) {
  return std::isfinite(v) && v > -NET_VEL_LIMIT && v < NET_VEL_LIMIT;
}

int net_seat_cap() {
  static int cap = 0;
  if (cap == 0) {
    cap = NET_PLAYER_CAP;
    const char *env = SDL_getenv("NEWTONIA_NET_TEST_SEATS");
    if (env) {
      int n = atoi(env);
      if (n >= 2 && n <= MAX_PLAYERS) cap = n;
    }
  }
  return cap;
}

bool net_state_sane(const Save::GameState &s) {
  // NaN/Inf must be rejected explicitly: every comparison against a NaN is
  // false, so a NaN world dimension would slip past the range checks below
  // and reach Grid()/WrappedPoint::set_boundaries(), where ceil(NaN/..) is
  // UB and rand()%0 faults.
  if (!std::isfinite(s.world_x) || !std::isfinite(s.world_y)) return false;
  if (s.world_x < 500.0f || s.world_x > 200000.0f) return false;
  if (s.world_y < 500.0f || s.world_y > 200000.0f) return false;
  // PROTO 25: up to MAX_PLAYERS seats on the wire (the live session count
  // is gated by net_seat_cap(); this is the SANITY bound, not the seat
  // policy). Seat ids, when present (v19+), must be
  // distinct and in range — a duplicate would fold two ships onto one
  // slot in the seat-keyed restore paths.
  if (s.players.size() < 1 || s.players.size() > (size_t)MAX_PLAYERS)
    return false;
  {
    uint8_t seat_mask = 0;
    for (size_t i = 0; i < s.players.size(); i++) {
      uint8_t seat = s.players[i].seat;
      if (seat == 0) continue;  // pre-v19 record: positional
      if (seat > MAX_PLAYERS) return false;
      uint8_t bit = (uint8_t)(1u << (seat - 1));
      if (seat_mask & bit) return false;
      seat_mask |= bit;
    }
  }
  // Time-slow countdown (PROTO 24): the apply clamps to the legal window;
  // reject only values no honest peer can produce (negative / absurd).
  if (s.time_slow_ms_left < 0 || s.time_slow_ms_left > 60000) return false;
  if (s.asteroids.size() > 5000) return false;
  if (s.pickups.size() > 500) return false;
  if (s.black_holes.size() > 16) return false;
  if (s.hazards.size() > 256) return false;
  if (s.station.enemies.size() > 64) return false;
  for (size_t i = 0; i < s.players.size(); i++) {
    if (s.players[i].primary_weapons.size() > 64) return false;
    if (s.players[i].secondary_weapons.size() > 64) return false;
  }
  // Every OTHER float in the state, for the same reason the world dimensions
  // are checked above: a NaN slips past every range comparison, and these
  // reach WrappedPoint, Object::radius and the collision grid. The coordinate
  // bound is deliberately far outside any legal value (the world caps at
  // 200000 and poses are wrapped into it) — it exists because Grid::get()
  // normalizes an out-of-range cell index by REPEATED ADDITION, so a merely
  // large-but-finite coordinate turns one grid query into tens of millions of
  // iterations. Velocities are bounded the same generous way: the wire's own
  // pose checks use ~3 units/ms, so 1e4 rejects only nonsense.
  //
  // Snapshots reach here from a peer, a replay file and a savegame; the
  // per-field validators in the MSG_/REC_ paths already do this and the
  // wholesale state rebuild is the one ingest that did not.
  for (size_t i = 0; i < s.players.size(); i++) {
    const Save::Player &p = s.players[i];
    if (!net_coord_sane(p.pos_x) || !net_coord_sane(p.pos_y)) return false;
    if (!net_vel_sane(p.vel_x) || !net_vel_sane(p.vel_y)) return false;
    if (!std::isfinite(p.facing_x) || !std::isfinite(p.facing_y)) return false;
  }
  for (size_t i = 0; i < s.asteroids.size(); i++) {
    const Save::Asteroid &a = s.asteroids[i];
    if (!net_coord_sane(a.pos_x) || !net_coord_sane(a.pos_y)) return false;
    if (!net_vel_sane(a.vel_x) || !net_vel_sane(a.vel_y)) return false;
    // Radius feeds the grid's cell span and every collision test. Only the
    // dangerous shapes are rejected (NaN, negative, absurd — the real cap is
    // Asteroid::max_radius, 240, so 10000 is 40x any legal value): a
    // rejection drops the WHOLE snapshot, so anything a legitimate breakup
    // chain could produce has to pass.
    if (!std::isfinite(a.radius) || a.radius < 0.0f || a.radius > 10000.0f)
      return false;
    if (!std::isfinite(a.rotation) || !std::isfinite(a.rotation_speed))
      return false;
    if (!std::isfinite(a.max_vertex_offset)) return false;
    for (int v = 0; v < 9; v++)
      if (!std::isfinite(a.vertex_offsets[v])) return false;
  }
  for (size_t i = 0; i < s.pickups.size(); i++)
    if (!net_coord_sane(s.pickups[i].pos_x) ||
        !net_coord_sane(s.pickups[i].pos_y)) return false;
  for (size_t i = 0; i < s.black_holes.size(); i++)
    if (!net_coord_sane(s.black_holes[i].pos_x) ||
        !net_coord_sane(s.black_holes[i].pos_y)) return false;
  for (size_t i = 0; i < s.hazards.size(); i++) {
    const Save::Hazard &h = s.hazards[i];
    // Kind must be one the receiver can draw and kill — an out-of-range
    // value builds an invisible, unkillable hazard that blocks the
    // level-clear gate forever (Hazard::from_state refuses it too; this
    // rejects the whole snapshot at the door like every other bound here).
    if (h.kind > (uint8_t)Hazard::HUNTER) return false;
    if (!net_coord_sane(h.pos_x) || !net_coord_sane(h.pos_y)) return false;
    if (!net_vel_sane(h.vel_x) || !net_vel_sane(h.vel_y)) return false;
    if (!std::isfinite(h.timer)) return false;
  }
  if (s.station.present) {
    const Save::Station &st = s.station;
    if (!net_coord_sane(st.pos_x) || !net_coord_sane(st.pos_y)) return false;
    if (!net_vel_sane(st.vel_x) || !net_vel_sane(st.vel_y)) return false;
    if (!std::isfinite(st.inner_rotation) || !std::isfinite(st.outer_rotation) ||
        !std::isfinite(st.time_until_next_ship)) return false;
    for (size_t i = 0; i < st.enemies.size(); i++) {
      const Save::Enemy &e = st.enemies[i];
      if (!net_coord_sane(e.pos_x) || !net_coord_sane(e.pos_y)) return false;
      if (!net_vel_sane(e.vel_x) || !net_vel_sane(e.vel_y)) return false;
      if (!std::isfinite(e.facing_x) || !std::isfinite(e.facing_y)) return false;
      if (!std::isfinite(e.thrust_force) || !std::isfinite(e.rotation_force))
        return false;
    }
  }
  if (s.mini_station.present) {
    const Save::MiniStation &m = s.mini_station;
    if (!net_coord_sane(m.pos_x) || !net_coord_sane(m.pos_y)) return false;
    if (!net_vel_sane(m.vel_x) || !net_vel_sane(m.vel_y)) return false;
    if (!std::isfinite(m.inner_rotation) || !std::isfinite(m.outer_rotation) ||
        !std::isfinite(m.time_until_next_shot)) return false;
  }
  return true;
}

// ---- handshake ----------------------------------------------------------

namespace {

// Milestone 1 has no build-fingerprint check (a per-build hash would break
// native<->web cross-play); the field is reserved on the wire.
const uint32_t BUILD_ID = 0;

// Connected transport but no valid HELLO/WELCOME inside this window means
// the peer is not a compatible game — give up rather than sit forever.
const int HANDSHAKE_TIMEOUT_MS = 10000;
// How long a held WELCOME waits for the worker's attestation before the
// admission gate stops waiting (NetSession::AdmitWait). Generous, and well
// inside HANDSHAKE_TIMEOUT_MS: the cost of waiting is a joiner sitting on
// CONNECTING a moment longer, the cost of being hasty is refusing someone
// who is not anonymous at all.
const int ADMIT_WAIT_MS = 4000;

// Test hook: send the pre-identity (short) HELLO/WELCOME so the e2e
// mixed-version drivers (test/e2e/identity_legacy.sh) can act as an old
// build without needing an old binary on disk.
bool identity_suppressed() {
  const char *e = std::getenv("NEWTONIA_NET_NO_IDENTITY");
  return e && e[0] && e[0] != '0';
}

// Test hook: send the identity with the name withheld (name_len 0) — the
// badge-only state. Valid on the wire by design: a console backend may
// withhold the display name while still sending the platform tag (the tag
// alone satisfies cross-network identifiability; names are optional
// everywhere). test/e2e/identity.sh asserts the badge-only path with this.
bool identity_name_withheld() {
  const char *e = std::getenv("NEWTONIA_NET_ANON_IDENTITY");
  return e && e[0] && e[0] != '0';
}

// Append-only identity extension shared by HELLO and WELCOME — see the
// PROTO_VERSION comment in net_protocol.h. Old peers ignore the trailing
// bytes; the parse side reads them only when present.
void append_identity(std::vector<uint8_t> &msg) {
  if (identity_suppressed()) return;
  const NetIdentity &id = net_local_identity();
  size_t n = identity_name_withheld() ? 0 : id.name.size();
  if (n > (size_t)NET_IDENTITY_NAME_MAX) n = NET_IDENTITY_NAME_MAX;
  Net::put_u8(msg, id.platform);
  Net::put_u8(msg, (uint8_t)n);
  if (n) Net::put_bytes(msg, id.name.data(), n);
}

// Parses the appended identity, tolerating every legacy/hostile shape: a
// short (old-build) message means "no identity", name_len 0 is a VALID
// name-withheld identity (platform known, badge-only — NOT the legacy
// case; renders as badge + generic fallback name), and a lying name_len must
// neither fault the reader nor fail the otherwise-valid handshake — the
// caller runs this only AFTER accepting the message, and ignores r.ok
// from here on.
void parse_identity(Net::Reader &r, NetIdentity &out) {
  out = NetIdentity();
  if (r.remaining() < 2) return;  // legacy peer: nothing appended
  uint8_t platform = r.u8();
  uint8_t name_len = r.u8();
  std::string name;
  if (name_len) {
    // bytes() is the single lying-length guard: it bounds-checks and
    // returns null on over-read (flipping r.ok, which the caller no
    // longer consults after accepting the message).
    const uint8_t *bytes = r.bytes(name_len);
    if (!bytes) return;  // inconsistent length: treat as no identity
    name.assign((const char *)bytes, name_len);
  }
  out.platform = platform;
  out.name = net_sanitize_name(name);  // cap + Typer glyph set, our side
  // Straight off the peer-to-peer wire: a self-report, so CLAIMED (rendered
  // only on a worker-less session). A withheld name stays ABSENT. The
  // worker's `identity` message later promotes fields to ATTESTED.
  out.platform_trust = NET_TRUST_CLAIMED;
  out.name_trust = out.name.empty() ? NET_TRUST_ABSENT : NET_TRUST_CLAIMED;
}

// One greppable line per handshake, the presence/invites log convention
// (test/e2e/identity.sh asserts on it). Display name only — never IDs.
void log_identity(const NetIdentity &id) {
  if (id.known())
    NET_LOG("net: identity peer name='%s' platform=%s(%u)\n",
            id.name.c_str(), net_platform_label(id.platform),
            (unsigned)id.platform);
  else
    NET_LOG("net: identity none (legacy peer)\n");
}

void send_hello(NetTransport *t) {
  std::vector<uint8_t> msg;
  Net::put_header(msg, Net::MSG_HELLO, 2);
  Net::put_u16(msg, Save::GameState::VERSION);
  Net::put_u32(msg, BUILD_ID);
  append_identity(msg);
  t->send_reliable(&msg[0], msg.size());
}

void send_welcome(NetTransport *t, uint8_t seat) {
  std::vector<uint8_t> msg;
  Net::put_header(msg, Net::MSG_WELCOME, 1);
  Net::put_u8(msg, seat);  // assigned seat (2 until the B4 lobby hands out 3..4)
  Net::put_u16(msg, 8);    // step_size (ms) — informational for now
  Net::put_u16(msg, 100);  // snapshot period (ms)
  append_identity(msg);
  t->send_reliable(&msg[0], msg.size());
}

void send_reject(NetTransport *t, uint8_t reason) {
  std::vector<uint8_t> msg;
  Net::put_header(msg, Net::MSG_REJECT, 1);
  Net::put_u8(msg, reason);
  t->send_reliable(&msg[0], msg.size());
}

}  // namespace

NetSession::NetSession(NetTransport *transport, Role role, int assign_seat)
    : transport_(transport),
      role_(role),
      phase_(Connecting),
      reject_reason_(0),
      hello_sent_(false),
      handshake_ms_(0) {
  // Host: the seat our WELCOME assigns (clamped defensively). Client: a
  // provisional 2 until WELCOME arrives (see the MSG_WELCOME handler).
  if (role_ == HostRole && assign_seat >= 2 && assign_seat <= MAX_PLAYERS)
    assigned_seat_ = (uint8_t)assign_seat;
}

NetSession::~NetSession() {
  if (transport_) {
    transport_->close();
    delete transport_;
  }
}

void NetSession::resolve_admission() {
  Admit a = admit_check_ ? admit_check_(peer_identity_) : AdmitAllow;
  if (a == AdmitWait) {
    if (admit_wait_ms_ < ADMIT_WAIT_MS) return;  // hold, ask again next tick
    a = AdmitAnonymous;  // the verdict never came (see Admit in the header)
  }
  if (a != AdmitAllow) {
    uint8_t reason = a == AdmitBanned ? RejectBanned : RejectAnonymous;
    // Both doors log this one line now (it used to be one per door). The
    // wording is load-bearing — three e2e drivers grep "refusing <kind>
    // pilot" (TESTING.md), and it is the only record of why a seat was
    // refused once the transport is gone.
    NET_LOG("net: refusing %s pilot at the handshake (seat %d)\n",
            a == AdmitBanned ? "banned" : "anonymous", (int)assigned_seat_);
    send_reject(transport_, reason);
    reject_reason_ = reason;
    phase_ = Rejected;
    return;
  }
  // Rejoin-by-identity: the installed resolver may re-map this WELCOME to
  // the parked seat whose remembered pilot the HELLO claim matches (see
  // set_seat_resolver). Out-of-range/0 answers keep the ctor seat. After
  // admission, so a refused pilot never reserves a seat.
  if (seat_resolver_) {
    int rs = seat_resolver_(peer_identity_);
    if (rs >= 2 && rs <= MAX_PLAYERS) assigned_seat_ = (uint8_t)rs;
  }
  send_welcome(transport_, assigned_seat_);
  phase_ = Ready;
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

  // A held WELCOME: everything this peer had to say arrived with its HELLO,
  // and admission is waiting on the worker's async attestation. Its own
  // clock, ahead of the quiet-peer timeout below (see admit_wait_ms_).
  if (hello_parsed_) {
    admit_wait_ms_ += delta_ms;
    resolve_admission();
    return;
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
      // Accept path only, AFTER the version gate: the identity append is
      // metadata and must never fail the handshake — but it IS the input
      // to the comms-policy gate (net_policy.h; default backend always
      // allows), which must run HERE, before the WELCOME goes out: refusing
      // after Ready would ghost the joiner on a CONNECTED screen it was
      // never entitled to, and every session adopter (lobby, mid-game
      // rejoin) inherits this single chokepoint instead of re-checking.
      parse_identity(r, peer_identity_);
      log_identity(peer_identity_);
      if (!net_comms_allowed_with(peer_identity_)) {
        NET_LOG("net: identity - peer refused by policy\n");
        send_reject(transport_, RejectNotAllowed);
        reject_reason_ = RejectNotAllowed;
        phase_ = Rejected;
        return;
      }
      // Everything the peer sends arrives here; the rest of the handshake
      // is this side deciding. Admission (the room's own ban list and
      // anonymous-players policy) may hold the WELCOME for a tick or more
      // while the worker's verdict catches up — see resolve_admission.
      hello_parsed_ = true;
      resolve_admission();
      return;
    }

    if (role_ == ClientRole && h.msg_type == Net::MSG_WELCOME) {
      uint8_t assigned = r.u8();
      (void)r.u16();  // step size
      (void)r.u16();  // snapshot period
      // PROTO 25: STORE the assigned seat (2..MAX_PLAYERS) instead of
      // validating literal 2 and discarding — player_id() returns it.
      if (!r.ok || assigned < 2 || assigned > MAX_PLAYERS) continue;
      assigned_seat_ = assigned;
      parse_identity(r, peer_identity_);
      log_identity(peer_identity_);
      // The client-side twin of the host's pre-WELCOME policy gate. There
      // is no client->host reject message; the host experiences the local
      // refusal as a peer that never sends INPUT and takes its normal
      // disconnect/rejoin path.
      if (!net_comms_allowed_with(peer_identity_)) {
        NET_LOG("net: identity - peer refused by policy\n");
        reject_reason_ = RejectNotAllowed;
        phase_ = Rejected;
        return;
      }
      phase_ = Ready;
      return;
    }

    if (role_ == ClientRole && h.msg_type == Net::MSG_REJECT) {
      reject_reason_ = r.u8();
      // Greppable, and the point of rejecting rather than closing: the
      // joiner can now say WHY it was turned away instead of guessing at
      // its own network. An unknown reason from a newer host still lands
      // here (and renders the generic refusal).
      NET_LOG("net: host refused this connection (reason %u)\n",
              (unsigned)reject_reason_);
      phase_ = Rejected;
      return;
    }
  }
}
