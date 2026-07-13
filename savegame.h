#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

// Binary save/load for a complete in-progress game.
// All I/O is handled in savegame.cpp; callers only work with the plain structs
// below and the four free functions at the bottom of this header.

namespace Save {

// ── Weapon ───────────────────────────────────────────────────────────────────

struct WeaponEntry {
    enum class Kind : uint8_t {
        Default,   // primary gun (identified by weapon_index into weapon_configs[])
        GodMode,   // also lives in primary list; ammo = ms remaining
        Mine,
        GigaMine,
        Missile,
        Shield,
        Nova,      // screen-clearing secondary; ammo = number of charges
        Beam,      // primary piercing-bolt gun; ammo = number of bolts
        Lance,     // primary full-length pulse; ammo = number of pulses
        Shock,     // chain-lightning primary; ammo = number of bolts (appended
                   // after Lance to keep the branch's v14 wire ordinals stable)
    };
    Kind kind;
    int  weapon_index;  // Default only; ignored for all other kinds
    int  ammo;
};

// ── Player ───────────────────────────────────────────────────────────────────

struct Player {
    int score, lives, kills, kills_this_life;
    bool respawning;  // true when saved mid-respawn countdown (alive==false, lives>0)
    float pos_x, pos_y;          // world position
    float facing_x, facing_y;    // unit direction vector
    float vel_x, vel_y;          // current velocity
    std::vector<WeaponEntry> primary_weapons;
    std::vector<WeaponEntry> secondary_weapons;
    int selected_primary_idx;    // index into primary_weapons
    int selected_secondary_idx;  // -1 when secondary iterator == end()
    int nova_charge;             // charge points toward next Nova bomb (0–9)
    int nova_kill_counter;       // kill sub-count toward next charge point (0–99)

    // v11 append — achievements bookkeeping (ACHIEVEMENTS.md). Written at the
    // end of the file, not inside the player block, per the append-only
    // convention; defaults keep older saves loading unchanged.
    int      asteroid_kills = 0;        // asteroids destroyed this game
    int      enemy_kills = 0;           // enemy ships destroyed this game
    bool     died_this_generation = false;
    uint32_t weapons_fired_mask = 0;    // bit = (int)WeaponEntry::Kind fired this game
};

// ── Asteroid ─────────────────────────────────────────────────────────────────

struct Asteroid {
    float pos_x, pos_y;
    float vel_x, vel_y;
    float radius, rotation, rotation_speed;
    int   value;
    int   health;
    float vertex_offsets[9];   // private in Asteroid, written via capture_state()
    float max_vertex_offset;

    // type flags
    bool invincible, invisible, reflective, teleporting, quantum, tough, elastic, armoured, phasing;

    // teleporting state (only meaningful when teleporting == true)
    bool  teleport_vulnerable;
    float teleport_angle;
    int   vulnerable_time_left;

    // quantum state (only meaningful when quantum == true)
    bool  quantum_observed;
    float quantum_base_speed;

    // armoured state (only meaningful when armoured == true)
    float armour_angle;

    // phasing state (only meaningful when phasing == true)
    bool  phased;
    int   phase_timer;

    // tough crack geometry (only meaningful when tough == true)
    int   crack_vertex[5];
    float crack_t[5];
    float crack_perp[5];
};

// ── Pickup ───────────────────────────────────────────────────────────────────

enum class PickupType : uint8_t {
    Weapon, Mine, GigaMine, Missile, Shield, GodMode, ExtraLife, NovaCharge, Beam, Lance,
    Revive,       // co-op: revives the fallen partner (v13)
    ShockWeapon   // chain-lightning primary drop (appended after Revive, v15)
};

struct Pickup {
    PickupType type;
    float pos_x, pos_y;
    int   weapon_index;  // only used when type == Weapon
};

// ── BlackHole ────────────────────────────────────────────────────────────────

struct BlackHole {
    float pos_x, pos_y;
};

// ── Enemy ship ───────────────────────────────────────────────────────────────

struct Enemy {
    float pos_x, pos_y;
    float vel_x, vel_y;
    float facing_x, facing_y;
    float thrust_force, rotation_force;
    int   value;
};

// ── Station ──────────────────────────────────────────────────────────────────

struct Station {
    bool  present;          // false = no station in this save
    bool  alive;
    int   lives;
    int   health;
    float pos_x, pos_y;
    float vel_x, vel_y;
    float inner_rotation, outer_rotation;
    int   wave, difficulty;
    int   ships_this_wave, ships_left_to_deploy;
    float time_until_next_ship;
    bool  deploying, redeploying;
    std::vector<Enemy> enemies;
};

// ── Mini-station ─────────────────────────────────────────────────────────────
// The small roaming station that appears once the black hole has been
// introduced. It drifts at a constant velocity (its single random direction)
// and fires at the nearest player on a fixed timer.

struct MiniStation {
    bool  present;          // false = no mini-station in this save
    bool  alive;
    float pos_x, pos_y;
    float vel_x, vel_y;     // constant drift velocity == direction it flies
    float inner_rotation, outer_rotation;
    float time_until_next_shot;
};

// ── Top-level game state ─────────────────────────────────────────────────────

struct GameState {
    static constexpr uint32_t MAGIC   = 0x4E57544E;  // "NWTN"
    // 12 = Beam/Lance weapon kinds, 13 = Revive pickup type, 14 = the
    // achievements append (master's "v11 append" renumbered: per-player
    // asteroid/enemy kills, died-this-generation, weapons-fired mask,
    // then the game-scoped cheat flag). 15 = Shock weapon kind + ShockWeapon
    // pickup type (merged from master, appended after the branch's additions).
    static constexpr uint16_t VERSION = 15;
    // Oldest save format we can still read. Saves from MIN_VERSION..VERSION all
    // load; anything older (or from a newer build) is ignored. To keep old saves
    // working across a version bump, only ever APPEND new fields at the end of
    // the file and read them back gated on `version >= N` (see load_game). That
    // way an older save simply stops short and the new fields take their
    // defaults. Loading then re-saving silently upgrades the file to VERSION.
    static constexpr uint16_t MIN_VERSION = 9;

    int   generation;
    float world_x, world_y;
    bool  level_cleared;
    int   time_until_next_generation;
    int   current_time;
    // v11 append (end of file): a cheat key was used this game — achievement
    // unlocks stay suppressed after resume (XR-057, ACHIEVEMENTS.md §1).
    bool  cheated = false;

    std::vector<Player>    players;
    std::vector<Asteroid>  asteroids;
    std::vector<Pickup>    pickups;
    std::vector<BlackHole> black_holes;
    Station                station;
    MiniStation            mini_station;
};

// ── Streams ──────────────────────────────────────────────────────────────────
// Byte-sink/source abstraction so the same serialization code drives both the
// save file and in-memory netplay snapshots (see NETPLAY.md). Both methods
// return false on failure so serialization can short-circuit with &&.

class Stream {
public:
    virtual ~Stream() {}
    virtual bool write(const void *data, size_t size) = 0;
    virtual bool read(void *data, size_t size) = 0;
};

class FileStream : public Stream {
public:
    explicit FileStream(std::FILE *file) : f(file) {}
    bool write(const void *data, size_t size) override;
    bool read(void *data, size_t size) override;
private:
    std::FILE *f;
};

class MemStream : public Stream {
public:
    MemStream() : pos(0) {}  // empty sink: write, then take data()
    explicit MemStream(std::vector<uint8_t> bytes)  // source: read from bytes
        : buf(std::move(bytes)), pos(0) {}
    bool write(const void *data, size_t size) override;
    bool read(void *data, size_t size) override;
    const std::vector<uint8_t> &data() const { return buf; }
private:
    std::vector<uint8_t> buf;
    size_t pos;
};

// ── API ───────────────────────────────────────────────────────────────────────

bool save_exists();
bool save_game(const GameState &state);  // returns false on I/O error
bool load_game(GameState &state);        // returns false if absent or format mismatch
void delete_save();

// Header-less body serialization: save_game/load_game wrap these with the
// MAGIC/VERSION header and the save file's path; netplay snapshots call them
// directly on a MemStream. deserialize_game takes the format version the
// bytes were written with (current VERSION when omitted).
// Per-struct serializers, exposed for the netplay delta protocol
// (NETPLAY.md M2-6): deltas carry individual players and new asteroids
// in exactly the save format.
bool write_player(Stream &f, const Player &p);
bool read_player(Stream &f, Player &p);
bool write_asteroid(Stream &f, const Asteroid &a);
bool read_asteroid(Stream &f, Asteroid &a);

bool serialize_game(Stream &out, const GameState &state);
bool deserialize_game(Stream &in, GameState &state,
                      uint16_t version = GameState::VERSION);

} // namespace Save
