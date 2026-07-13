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
        Nova,      // screen-clearing secondary; ammo = number of charges
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
    Weapon, Mine, GigaMine, Missile, Shield, GodMode, ExtraLife, NovaCharge
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

// ── Hazard ───────────────────────────────────────────────────────────────────
// The environmental obstacles introduced on the mid-game "quiet" levels
// (generations 9/11/12). One struct for all kinds; `kind` selects the
// behaviour (see hazard.h). `timer` carries the PULSAR shockwave phase;
// `vel_*` the COMET/SEEKER travel velocity (PULSAR is stationary).

struct Hazard {
    uint8_t kind;               // Hazard::Kind
    float   pos_x, pos_y;
    float   vel_x, vel_y;
    float   timer;              // PULSAR shockwave phase
    int32_t health;             // COMET shots remaining (0 for other kinds)
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
    static constexpr uint16_t VERSION = 12;
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
    // v12 append (end of file): mid-game hazards (pulsar/comet/seeker).
    std::vector<Hazard>    hazards;
};

// ── API ───────────────────────────────────────────────────────────────────────

bool save_exists();
bool save_game(const GameState &state);  // returns false on I/O error
bool load_game(GameState &state);        // returns false if absent or format mismatch
void delete_save();

} // namespace Save
