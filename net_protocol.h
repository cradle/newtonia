#ifndef NET_PROTOCOL_H
#define NET_PROTOCOL_H

// Netplay wire protocol — see NETPLAY.md ("Protocol quick-ref").
//
// Every message starts with a 4-byte header:
//   uint8 proto_version (= PROTO_VERSION)
//   uint8 msg_type      (Net::MsgType)
//   uint8 player_id     (sender's seat 1..MAX_PLAYERS since PROTO 25;
//                        for relayed SHOT/LANCE/SHOCK effects it is the
//                        FIRER's seat, which the receiver resolves)
//   uint8 reserved      (0)
// All multi-byte fields are little-endian and packed/unpacked explicitly
// byte-by-byte — structs are never memcpy'd onto the wire (native and web
// builds must interoperate regardless of compiler padding).

#include <stdint.h>

#include <chrono>
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
  if (on < 0) {
    const char *e = std::getenv("NEWTONIA_NET_DEBUG");
    on = (e && e[0]) ? 1 : 0;
    // Windows sessions have nowhere to see stdout (GUI/Steam launch
    // loses it — "unfortunately no log was written"): point
    // NEWTONIA_NET_LOG_FILE at a path and everything printed lands
    // there instead.
    if (on) {
      const char *f = std::getenv("NEWTONIA_NET_LOG_FILE");
      if (f && f[0] && !std::freopen(f, "w", stdout)) {
        // stdout unchanged on failure; nothing sane to report to.
      }
    }
  }
  return on != 0;
}

// Which side of the wire this process is, stamped in front of every
// NET_LOG line so interleaved host+client logs (side-by-side captures
// from both machines) attribute themselves. Set at the lobby's
// host/join decision and again when GLGame enters a net mode; empty
// until a role exists.
inline const char *&net_log_role() {
  static const char *role = "";
  return role;
}
inline void set_net_log_role(bool host) {
  net_log_role() = host ? "host: " : "client: ";
}

// Seconds since this process's first net log line: gap/stall events on
// a timeline expose any periodicity (TURN permission refresh 240 s,
// channel rebind 600 s, ICE consent ~15 s all have fingerprint cadences).
inline double net_log_secs() {
  static const std::chrono::steady_clock::time_point t0 =
      std::chrono::steady_clock::now();
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
      .count();
}

}  // namespace Net

// Both printfs land in the same stdio buffer and flush as one write, so
// two processes sharing a terminal don't shear a line apart.
#define NET_LOG(...) \
  do { if (::Net::net_debug_enabled()) { std::printf("%s%9.3f ", ::Net::net_log_role(), ::Net::net_log_secs()); std::printf(__VA_ARGS__); std::fflush(stdout); } } while (0)

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
// 10: bullet-vs-asteroid impact cues are client-side cosmetic
//     (Ship::net_cosmetic_impacts); the host no longer sends EV_ROID_*
//     for them — a mixed pairing would double or drop those cues.
// 11: MSG_PING/MSG_PONG — 1 Hz unreliable RTT probe from each side; the
//     client's local-ship blend latency-compensates with it.
// 12: client-authoritative ship pose — MSG_INPUT carries position/velocity
//     plus a warp-count echo; the host adopts the reported pose, so the
//     pilot is never corrected (an input blackout shows on the REMOTE view
//     of that ship instead of rubberbanding its own player).
// 13: client-authoritative bullet kills — MSG_HIT (C->H rel, uint32
//     asteroid net_id) claims a would-kill hit the client's screen saw;
//     the client kills its copy instantly and the host honors the claim
//     (kill + credit), keeping fragments/drops/score host-owned. Ends
//     "shots that don't count" when the host's copy of the bullet missed
//     (pose divergence, stall-delayed inputs).
// 14: client-authoritative shot spawning — MSG_SHOT (C->H rel: uint32
//     shot id, spawn pos, exact velocity with spread applied, flags)
//     replaces the host's re-simulation of the client's gun (independent
//     rand() spread flew every shot's two copies on different headings);
//     the host spawns exact clones. MSG_HIT gains the bullet id for
//     precise consume of the killing bullet's host copy.
// 15: client bullet hits on enemy ships / stations — MSG_HIT_SHIP
//     (C->H rel: kind, bullet id, impact pos). The client consumes the
//     bullet at visible contact (they used to sail straight through
//     replicated ships); the host applies the damage IFF it consumes
//     the referenced clone — exactly-once per shot by construction.
// 16: per-enemy ids — the extras' enemy section carries each deployed
//     enemy's net_ship_id (client re-stamps its rebuilt replicas every
//     apply), MSG_HIT_SHIP gains the target id so enemy claims kill the
//     EXACT enemy (no nearest-to-impact guessing), and the client kills
//     its replica instantly with resurrection suppression — enemy
//     deaths get the asteroid treatment.
// 17: MSG_SHOT flows BOTH ways. The host echoes its player's shots to the
//     client (same wire format), which spawns exact clones instantly —
//     host bullets used to exist client-side only via the 10 Hz snapshot
//     rebuild, so each one popped in up to a snapshot interval late and
//     already down-range (obvious when spectating at the host's muzzle).
//     The echo also carries the host's gun sound to the client.
// 18: Pierce Beam + Lance go online. MSG_SHOT flags gain bit2 = piercing
//     (clone bolts plough through kills on both sides) and the snapshot's
//     per-bullet records gain a flags byte so piercing/trail/kills_invincible
//     survive the 10 Hz rebuild. New MSG_LANCE (both ways) carries a fired
//     lance pulse's traced polyline for the peer's flash + sound; the
//     lance's kills ride the existing claim machinery — a client lance
//     queues MSG_HIT claims with bullet_id 0 (sentinel: no clone to
//     consume) and its ray-march predicts outcomes without killing locally
//     (the claim drain kills, exactly like bullet claims).
// 19: Snapshot per-bullet flags byte gains bit3 = world_bullet, so a shot
//     the host sim ricocheted off a reflective/armoured surface recolours
//     white on the client too. New MSG_BOUNCE (H->C): the sim's real
//     bounce of any id-carrying bullet (entry back-trace, edge normal,
//     asteroid reference frame) overrides the client's local radial
//     approximation, so the two copies of a ricocheted shot fly the SAME
//     post-bounce trajectory instead of merely sharing a colour.
// 20: Lance ship hits. MSG_HIT_SHIP gains the bullet_id-0 sentinel (the
//     twin of MSG_HIT's): a client lance kills enemy replicas instantly
//     and claims them with no clone to consume — enemy kills are
//     idempotent by wire id, so the host's own resolution of the same
//     MSG_LANCE polyline (self-kill on reflection, partner under friendly
//     fire, station hull damage) can never double-count them.
// 21: Snapshot GameState grew the save-v14 achievements append (per-player
//     asteroid/enemy kills, died-this-generation, weapons-fired mask, the
//     game-scoped cheat flag) — serialize/deserialize_game are shared
//     between saves and the wire, so the snapshot layout changed with it.
// 22: Shock weapon online. MSG_SHOCK (both ways) carries a completed
//     chain-lightning bolt's polyline for the remote flash + zap sound (the
//     firer's growing seek is local-only; the receiver shows the finished
//     bolt fading). The bolt's KILLS reuse the existing claim paths exactly
//     like the lance: the client claims asteroid kills via MSG_HIT (bullet_id
//     0) and enemy kills via MSG_HIT_SHIP (bullet_id 0); the host resolves
//     its own player's kills locally and applies station/mini-station hull
//     damage from the received polyline. The host suppresses the remote
//     ship's local shock sim (net_remote_gun) so bolts come only from the wire.
// 23: savegame v17 appended run_id (REPLAY.md) — snapshots serialize through
//     the same structs, so every keyframe/delta grew 8 bytes.
// 24: Time-slow pickup online. Savegame v18 appended the TimeSlow pickup
//     type and the in-flight effect (sim ms remaining + owning player
//     index) to the GameState — snapshots share the struct, so every
//     keyframe/delta grew 5 bytes. No new message: the host runs the
//     effect and the countdown rides every snapshot; both sides multiply
//     their step SCHEDULING by the same factor from that scalar (the sim
//     still advances step_size per step), so the whole session slows in
//     lockstep and the collector's rotation comp is re-asserted from the
//     owner index on each apply.
// 25: The 4-player seat flag day (FOURPLAYER.md PB-D3) — a 2-player wire
//     in practice until B7's cap flip, but every seat-shaped byte becomes
//     meaningful so B4's fan-out is additive: WELCOME's assigned-id byte is
//     STORED by the client (NetSession::player_id() returns it) instead of
//     validated-and-discarded; snapshot ship records (GameState players via
//     savegame v19's seat append, and each nx ship-extras record) lead with
//     the seat id, replacing the positional zips; the header player_id byte
//     is stamped with the sender's real seat (the host stamped literal 2 on
//     its own SHOT/LANCE/SHOCK echoes — nothing read it) and the receivers
//     of relayed effects resolve the firer BY that seat; bullet ids are
//     partitioned by seat in the top nibble so cross-ship scans stay
//     unambiguous; EV_SHIP_IMPACT's arg reads as seat 1..MAX_PLAYERS;
//     EV_REMOTE_SHOT (dead since PROTO 17) loses its reader.
// 26: Turret drones (savegame v20). Each nx ship-extras record gains a
//     trailing turret section (pos/vel, barrel angle, ms left, cooldown,
//     shots left) — host-echoed like mines, deploy-grace held like every
//     secondary. No new message: turret bullets are ordinary bullets from
//     the owner's gun path, so MSG_SHOT reporting, hit claims and the
//     wholesale bullet rebuild all cover them unchanged; the firing sim's
//     mint gate (is_local_player / net_remote_gun) decides which machine
//     spawns the real bullet, and both sides run the same aim/ammo
//     bookkeeping so expiry stays in step.
// 27: savegame v21 appended per-player DEPLOYED turret lists to the
//     GameState — snapshots serialize through the same structs, so every
//     keyframe/delta grows ~29 bytes per live drone. The client never
//     APPLIES that copy (Ship::restore_state gates the rebuild to offline /
//     host-resume loads): online the nx ship-extras section remains the
//     authoritative turret feed, with its deploy grace and vanish
//     detection. The body copy exists so save/continue — and the online
//     host's resume slot — bring a battery back.
const uint8_t PROTO_VERSION = 27;

// Peer identity (badge metadata) rides HELLO/WELCOME as an APPEND, not a
// PROTO bump: `u8 platform (NetPlatform), u8 name_len, name_len bytes
// UTF-8` at the END of both messages. A bump would flag-day the live
// cross-platform pool for what is pure display metadata — an old peer that
// never learns your platform is still a fully playable partner. Reader
// tolerates trailing bytes, so old builds ignore the append for free; new
// builds parse it ONLY when `remaining() > 0` and treat absence (or a
// lying name_len) as "no identity" — the savegame append-only convention
// applied to the wire. `name_len == 0` is VALID and distinct from absence:
// platform known, name deliberately withheld (display names are optional;
// some platform backends send badge-only). See net_identity.h /
// net_session.cpp.

enum MsgType {
  MSG_HELLO = 1,           // C->H rel: proto + save version + build check
                           //   + appended identity (see above)
  MSG_WELCOME = 2,         // H->C rel: assigned player_id, timing constants
                           //   + appended identity (see above)
  MSG_REJECT = 3,          // H->C rel: version/build mismatch, lobby full
  MSG_INPUT = 4,           // C->H unrel: per-tick input state
  MSG_SNAPSHOT_CHUNK = 5,  // H->C rel: chunked KEYFRAME snapshot, 1 Hz
  MSG_DELTA = 7,           // H->C rel: between-keyframe delta, 10 Hz
  MSG_EVENT = 6,           // both ways, rel: EventCode below
  // RTT probe, both ways on the UNRELIABLE channel (the reliable one
  // would fold head-of-line stalls into the reading): PING carries the
  // sender's SDL_GetTicks, PONG echoes it straight back.
  MSG_PING = 8,
  MSG_PONG = 9,
  // Client hit claim (PROTO 13): uint32 asteroid net_id the local ship's
  // bullet visibly killed, + uint32 bullet net_id (PROTO 14) so the host
  // consumes exactly the killing bullet's clone. RELIABLE — a claim must
  // survive the exact stall conditions that delay everything else.
  MSG_HIT = 10,
  // Shot report, BOTH ways since PROTO 17: uint32 shot id, 2x float spawn
  // pos, 2x float velocity (spread already applied), uint8 flags (bit0
  // kills_invincible, bit1 trail, bit2 piercing — PROTO 18 beam bolt).
  // RELIABLE + ordered, so a MSG_HIT can never arrive before the shot it
  // references. C->H (PROTO 14): the host spawns exact clones of the
  // client's shots. H->C (PROTO 17): the client spawns exact clones of the
  // host's shots the moment they fire, instead of waiting for the next
  // 10 Hz snapshot rebuild.
  MSG_SHOT = 11,
  // Client bullet-vs-ship hit claim (PROTO 15/16): uint8 kind (0 enemy,
  // 1 station, 2 mini-station), uint32 bullet net_id, uint32 target id
  // (the enemy's net_ship_id; 0 for the singleton stations), 2x float
  // impact pos. Damage applies IFF the host consumes the referenced
  // clone — a clone its own sim already resolved makes the claim a
  // no-op, so every shot resolves exactly once.
  MSG_HIT_SHIP = 12,
  // Lance pulse report (PROTO 18), BOTH ways rel: u8 n_points (2..17),
  // n x 2 float polyline the firer's ray-march traced. Display-only on
  // the receiver — the pulse's KILLS travel as MSG_HIT claims (client
  // firer, bullet_id 0) or ordinary removal records (host firer); this
  // message is the flash and the sound.
  MSG_LANCE = 13,
  // Authoritative bullet-state override (PROTO 19), H->C rel: uint32
  // bullet net_id, 2x float pos, 2x float velocity, uint8 flags (the
  // Particle net_flags byte). Sent whenever the host sim RICOCHETS an
  // id-carrying bullet (reflective / armoured-face deflection) so the
  // client snaps its copy onto the real post-bounce trajectory instead
  // of keeping its local radial approximation. Deliberately general —
  // any future host-side bullet redirection (gravity slingshots, new
  // deflectors) can reuse it as-is. Unknown ids are ignored (the bullet
  // already expired or was consumed client-side).
  MSG_BOUNCE = 14,
  // Shock bolt report (PROTO 22), BOTH ways rel: u8 n_points (2..15),
  // n x 2 float polyline of a completed chain-lightning bolt. Display-only
  // on the receiver (the finished bolt fading) plus the zap sound; the
  // bolt's KILLS travel as MSG_HIT / MSG_HIT_SHIP claims (client firer,
  // bullet_id 0) or the host firer's own resolution, and station/mini hull
  // damage from a client bolt is applied host-side from this polyline.
  MSG_SHOCK = 15,
  // Seat identity relay (post-B7 4P HUD), H->C rel: u8 seat (2..MAX_PLAYERS),
  // u8 platform (NetPlatform), u8 platform_trust, u8 name_trust (NetTrust),
  // u8 name_len, name_len bytes UTF-8, then a TRAILING u8 flags (bit0 =
  // the seat paired through the host's LAN door, so the per-peer offline
  // display carve-out applies — net_id_ctx_for_seat). Flags is trailing
  // because this message already shipped at PROTO 25 without it — the
  // savegame append rule applied to a message body: an old reader stops
  // short of the extra byte, a new reader treats absence as flags 0
  // (no carve-out, the under-render direction). The host shares each remote seat's
  // badge identity with every client, so a client's HUD can name the OTHER
  // clients (its own handshake only ever carried the host's). Display
  // metadata only, like the HELLO/WELCOME identity append — and like that
  // append it is deliberately NOT a PROTO bump: an older receiver ignores
  // the unknown message type and renders role labels, which is exactly the
  // legacy-peer fallback. Trust levels are the host's assertion; the host
  // already holds the worker attestations (and is sim-authoritative for far
  // more than a name), and the receiver re-sanitizes the name bytes and
  // clamps unknown trust values down to CLAIMED so a lying host can at
  // worst under-render on this axis. Sent to each client at its first
  // INPUT (the per-peer resync point) and re-broadcast whenever a seat's
  // identity changes (attestation lands, rejoin refresh).
  MSG_PEER_IDENT = 16,
};

enum EventCode {
  EV_PAUSE = 1,
  EV_RESUME = 2,
  EV_GENERATION_START = 3,  // + uint32 generation
  EV_GAME_OVER = 4,
  EV_BYE = 5,
  // Impact thud with a packed position (pack_pos). Since PROTO 10 the
  // bullet-vs-asteroid impact cues are client-side cosmetic
  // (Ship::net_cosmetic_impacts) and only the gen-20 station-hull
  // deflection still sends this; EV_ROID_TING is retired (kept for
  // numbering).
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
  // dying; arg = seat 1..MAX_PLAYERS | 0x100 when the armoured-face
  // ting applies (widened from 1|2 at PROTO 25).
  EV_SHIP_IMPACT = 11,
  // Retired at PROTO 25 (kept for numbering, like EV_ROID_TING): the
  // MSG_SHOT echo superseded it at PROTO 17 and nothing wrote it since;
  // old replay files may still carry code 12 — the reader's default
  // case drops it silently.
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
  // Host -> client: the host's simulation detected an achievement unlock
  // it attributes to the CLIENT's ship (ram kills and station kills only
  // resolve host-side). arg = AchRelay; the client's own cheat
  // suppression still applies inside unlock().
  EV_ACHIEVEMENT = 17,
  // Host -> client: a surviving (shielded) ram by the CLIENT's ship killed
  // an asteroid, and the sim detonate()d a bullet burst into that ship's
  // list. The client skips its own-ship bullet echo (locally simulated,
  // PROTO 14), so the burst was invisible on the very machine that rammed;
  // on receipt the client mints the blast locally via net_blast() — real
  // bullets, instant kills, bullet_id-0 claims, exactly the own-mine-
  // explosion treatment. Fatal rams need no relay (the extras' death
  // detonate already runs client-side). No arg.
  EV_RAM_BLAST = 18,
  // H->C, targeted: the host removed this peer from the room. Behaves like
  // a BYE for the receiver (terminal, no auto-rejoin — a kicked client that
  // reconnected on its own would undo the kick), with its own wording so
  // "you were removed" doesn't read as "the host left". Appended, not a
  // PROTO bump: net_handle_event ignores unknown codes, so an older client
  // just sees the transport close and tries to rejoin — the seat may well
  // be gone by then, and the host is free to kick again.
  EV_KICKED = 19,
};

// EV_ACHIEVEMENT arg values. Stable wire numbers — append only.
enum AchRelay {
  ACH_SHIELD_RAM = 1,
  ACH_SHIELD_RAM_ASTEROID = 2,
  ACH_MINI_STATION_KILL = 3,
  ACH_STATION_DESTROYED = 4,
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
