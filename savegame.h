#pragma once
#include <cstdint>
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
    };
    Kind kind;
    int  weapon_index;  // Default only; ignored for all other kinds
    int  ammo;
};

// ── Player ───────────────────────────────────────────────────────────────────

struct Player {
    int score, lives, kills, kills_this_life;
    float pos_x, pos_y;          // world position
    float facing_x, facing_y;    // unit direction vector
    float vel_x, vel_y;          // current velocity
    std::vector<WeaponEntry> primary_weapons;
    std::vector<WeaponEntry> secondary_weapons;
    int selected_primary_idx;    // index into primary_weapons
    int selected_secondary_idx;  // -1 when secondary iterator == end()
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
    bool invincible, invisible, reflective, teleporting, quantum, tough, elastic;

    // teleporting state (only meaningful when teleporting == true)
    bool  teleport_vulnerable;
    float teleport_angle;
    int   vulnerable_time_left;

    // quantum state (only meaningful when quantum == true)
    bool  quantum_observed;
    float quantum_base_speed;

    // tough crack geometry (only meaningful when tough == true)
    int   crack_vertex[5];
    float crack_t[5];
    float crack_perp[5];
};

// ── Pickup ───────────────────────────────────────────────────────────────────

enum class PickupType : uint8_t {
    Weapon, Mine, GigaMine, Missile, Shield, GodMode, ExtraLife
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

// ── Top-level game state ─────────────────────────────────────────────────────

struct GameState {
    static constexpr uint32_t MAGIC   = 0x4E57544E;  // "NWTN"
    static constexpr uint16_t VERSION = 4;

    int   generation;
    float world_x, world_y;
    bool  level_cleared;
    int   time_until_next_generation;
    int   current_time;

    std::vector<Player>    players;
    std::vector<Asteroid>  asteroids;
    std::vector<Pickup>    pickups;
    std::vector<BlackHole> black_holes;
    Station                station;
};

// ── API ───────────────────────────────────────────────────────────────────────

bool save_exists();
bool save_game(const GameState &state);  // returns false on I/O error
bool load_game(GameState &state);        // returns false if absent or format mismatch
void delete_save();

} // namespace Save
