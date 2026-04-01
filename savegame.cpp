#include "savegame.h"
#include <SDL.h>
#include <cstdio>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static const char *SG_ORG  = "cc.gfm";
static const char *SG_APP  = "newtonia";
static const char *SG_FILE = "savegame.dat";

// ── Path helper ──────────────────────────────────────────────────────────────

static std::string save_path() {
    char *dir = SDL_GetPrefPath(SG_ORG, SG_APP);
    if (!dir) return "";
    std::string path = std::string(dir) + SG_FILE;
    SDL_free(dir);
    return path;
}

// ── Low-level I/O helpers ────────────────────────────────────────────────────
// All return false on failure so callers can short-circuit with &&.

template<typename T>
static bool wv(FILE *f, const T &v) { return fwrite(&v, sizeof(T), 1, f) == 1; }

template<typename T>
static bool rv(FILE *f, T &v)       { return fread(&v,  sizeof(T), 1, f) == 1; }

template<typename T, int N>
static bool wa(FILE *f, const T (&a)[N]) { return fwrite(a, sizeof(T), N, f) == (size_t)N; }

template<typename T, int N>
static bool ra(FILE *f, T (&a)[N])       { return fread(a,  sizeof(T), N, f) == (size_t)N; }

// ── Per-type write/read ───────────────────────────────────────────────────────

static bool write_weapon(FILE *f, const Save::WeaponEntry &w) {
    return wv(f, (uint8_t)w.kind) && wv(f, (int32_t)w.weapon_index) && wv(f, (int32_t)w.ammo);
}

static bool read_weapon(FILE *f, Save::WeaponEntry &w) {
    uint8_t kind; int32_t wi, ammo;
    if (!rv(f, kind) || !rv(f, wi) || !rv(f, ammo)) return false;
    w.kind         = (Save::WeaponEntry::Kind)kind;
    w.weapon_index = (int)wi;
    w.ammo         = (int)ammo;
    return true;
}

static bool write_player(FILE *f, const Save::Player &p) {
    if (!wv(f, (int32_t)p.score))           return false;
    if (!wv(f, (int32_t)p.lives))           return false;
    if (!wv(f, (int32_t)p.kills))           return false;
    if (!wv(f, (int32_t)p.kills_this_life)) return false;
    if (!wv(f, p.pos_x) || !wv(f, p.pos_y))             return false;
    if (!wv(f, p.facing_x) || !wv(f, p.facing_y))       return false;
    if (!wv(f, p.vel_x) || !wv(f, p.vel_y))             return false;

    uint32_t pc = (uint32_t)p.primary_weapons.size();
    if (!wv(f, pc)) return false;
    for (const auto &w : p.primary_weapons)
        if (!write_weapon(f, w)) return false;
    if (!wv(f, (int32_t)p.selected_primary_idx)) return false;

    uint32_t sc = (uint32_t)p.secondary_weapons.size();
    if (!wv(f, sc)) return false;
    for (const auto &w : p.secondary_weapons)
        if (!write_weapon(f, w)) return false;
    if (!wv(f, (int32_t)p.selected_secondary_idx)) return false;

    return true;
}

static bool read_player(FILE *f, Save::Player &p) {
    int32_t score = 0, lives = 0, kills = 0, kills_this_life = 0;
    if (!rv(f, score) || !rv(f, lives) || !rv(f, kills) || !rv(f, kills_this_life)) return false;
    p.score = score; p.lives = lives; p.kills = kills; p.kills_this_life = kills_this_life;
    if (!rv(f, p.pos_x) || !rv(f, p.pos_y))       return false;
    if (!rv(f, p.facing_x) || !rv(f, p.facing_y)) return false;
    if (!rv(f, p.vel_x) || !rv(f, p.vel_y))       return false;

    uint32_t pc;
    if (!rv(f, pc)) return false;
    p.primary_weapons.resize(pc);
    for (auto &w : p.primary_weapons)
        if (!read_weapon(f, w)) return false;
    int32_t si;
    if (!rv(f, si)) return false;
    p.selected_primary_idx = (int)si;

    uint32_t sc;
    if (!rv(f, sc)) return false;
    p.secondary_weapons.resize(sc);
    for (auto &w : p.secondary_weapons)
        if (!read_weapon(f, w)) return false;
    if (!rv(f, si)) return false;
    p.selected_secondary_idx = (int)si;

    return true;
}

static bool write_asteroid(FILE *f, const Save::Asteroid &a) {
    if (!wv(f, a.pos_x) || !wv(f, a.pos_y)) return false;
    if (!wv(f, a.vel_x) || !wv(f, a.vel_y)) return false;
    if (!wv(f, a.radius) || !wv(f, a.rotation) || !wv(f, a.rotation_speed)) return false;
    if (!wv(f, (int32_t)a.value)) return false;
    if (!wv(f, (int32_t)a.health)) return false;
    if (!wa(f, a.vertex_offsets)) return false;
    if (!wv(f, a.max_vertex_offset)) return false;

    // Pack all type flags into one byte
    uint8_t flags = 0;
    if (a.invincible)  flags |= (1 << 0);
    if (a.invisible)   flags |= (1 << 1);
    if (a.reflective)  flags |= (1 << 2);
    if (a.teleporting) flags |= (1 << 3);
    if (a.quantum)     flags |= (1 << 4);
    if (a.tough)       flags |= (1 << 5);
    if (a.elastic)     flags |= (1 << 6);
    if (a.armoured)    flags |= (1 << 7);
    if (!wv(f, flags)) return false;

    if (a.teleporting) {
        if (!wv(f, (uint8_t)a.teleport_vulnerable)) return false;
        if (!wv(f, a.teleport_angle)) return false;
        if (!wv(f, (int32_t)a.vulnerable_time_left)) return false;
    }
    if (a.quantum) {
        if (!wv(f, (uint8_t)a.quantum_observed)) return false;
        if (!wv(f, a.quantum_base_speed)) return false;
    }
    if (a.armoured) {
        if (!wv(f, a.armour_angle)) return false;
    }
    if (a.tough) {
        if (!wa(f, a.crack_vertex)) return false;
        if (!wa(f, a.crack_t)) return false;
        if (!wa(f, a.crack_perp)) return false;
    }
    return true;
}

static bool read_asteroid(FILE *f, Save::Asteroid &a) {
    if (!rv(f, a.pos_x) || !rv(f, a.pos_y)) return false;
    if (!rv(f, a.vel_x) || !rv(f, a.vel_y)) return false;
    if (!rv(f, a.radius) || !rv(f, a.rotation) || !rv(f, a.rotation_speed)) return false;
    int32_t val, health;
    if (!rv(f, val) || !rv(f, health)) return false;
    a.value = (int)val; a.health = (int)health;
    if (!ra(f, a.vertex_offsets)) return false;
    if (!rv(f, a.max_vertex_offset)) return false;

    uint8_t flags;
    if (!rv(f, flags)) return false;
    a.invincible  = (flags >> 0) & 1;
    a.invisible   = (flags >> 1) & 1;
    a.reflective  = (flags >> 2) & 1;
    a.teleporting = (flags >> 3) & 1;
    a.quantum     = (flags >> 4) & 1;
    a.tough       = (flags >> 5) & 1;
    a.elastic     = (flags >> 6) & 1;
    a.armoured    = (flags >> 7) & 1;

    if (a.teleporting) {
        uint8_t tv; int32_t vtl;
        if (!rv(f, tv) || !rv(f, a.teleport_angle) || !rv(f, vtl)) return false;
        a.teleport_vulnerable  = (bool)tv;
        a.vulnerable_time_left = (int)vtl;
    } else {
        a.teleport_vulnerable  = false;
        a.teleport_angle       = 0.0f;
        a.vulnerable_time_left = 0;
    }

    if (a.quantum) {
        uint8_t qo;
        if (!rv(f, qo) || !rv(f, a.quantum_base_speed)) return false;
        a.quantum_observed = (bool)qo;
    } else {
        a.quantum_observed  = false;
        a.quantum_base_speed = 0.0f;
    }

    if (a.armoured) {
        if (!rv(f, a.armour_angle)) return false;
    } else {
        a.armour_angle = 0.0f;
    }
    if (a.tough) {
        if (!ra(f, a.crack_vertex) || !ra(f, a.crack_t) || !ra(f, a.crack_perp)) return false;
    }

    return true;
}

static bool write_pickup(FILE *f, const Save::Pickup &p) {
    return wv(f, (uint8_t)p.type) && wv(f, p.pos_x) && wv(f, p.pos_y) && wv(f, (int32_t)p.weapon_index);
}

static bool read_pickup(FILE *f, Save::Pickup &p) {
    uint8_t type; int32_t wi;
    if (!rv(f, type) || !rv(f, p.pos_x) || !rv(f, p.pos_y) || !rv(f, wi)) return false;
    p.type         = (Save::PickupType)type;
    p.weapon_index = (int)wi;
    return true;
}

static bool write_enemy(FILE *f, const Save::Enemy &e) {
    return wv(f, e.pos_x) && wv(f, e.pos_y)
        && wv(f, e.vel_x) && wv(f, e.vel_y)
        && wv(f, e.facing_x) && wv(f, e.facing_y)
        && wv(f, e.thrust_force) && wv(f, e.rotation_force)
        && wv(f, (int32_t)e.value);
}

static bool read_enemy(FILE *f, Save::Enemy &e) {
    int32_t value = 0;
    if (!rv(f, e.pos_x) || !rv(f, e.pos_y)
     || !rv(f, e.vel_x) || !rv(f, e.vel_y)
     || !rv(f, e.facing_x) || !rv(f, e.facing_y)
     || !rv(f, e.thrust_force) || !rv(f, e.rotation_force)
     || !rv(f, value)) return false;
    e.value = (int)value;
    return true;
}

static bool write_station(FILE *f, const Save::Station &s) {
    if (!wv(f, (uint8_t)s.present)) return false;
    if (!s.present) return true;
    if (!wv(f, (uint8_t)s.alive)) return false;
    if (!wv(f, (int32_t)s.lives)) return false;
    if (!wv(f, (int32_t)s.health)) return false;
    if (!wv(f, s.pos_x) || !wv(f, s.pos_y)) return false;
    if (!wv(f, s.vel_x) || !wv(f, s.vel_y)) return false;
    if (!wv(f, s.inner_rotation) || !wv(f, s.outer_rotation)) return false;
    if (!wv(f, (int32_t)s.wave) || !wv(f, (int32_t)s.difficulty)) return false;
    if (!wv(f, (int32_t)s.ships_this_wave) || !wv(f, (int32_t)s.ships_left_to_deploy)) return false;
    if (!wv(f, s.time_until_next_ship)) return false;
    if (!wv(f, (uint8_t)s.deploying) || !wv(f, (uint8_t)s.redeploying)) return false;
    uint32_t cnt = (uint32_t)s.enemies.size();
    if (!wv(f, cnt)) return false;
    for (const auto &e : s.enemies)
        if (!write_enemy(f, e)) return false;
    return true;
}

static bool read_station(FILE *f, Save::Station &s) {
    uint8_t present = 0;
    if (!rv(f, present)) return false;
    s.present = (bool)present;
    if (!s.present) return true;
    uint8_t alive = 0, deploying = 0, redeploying = 0;
    int32_t lives = 0, health = 0, wave = 0, difficulty = 0, ships_this_wave = 0, ships_left = 0;
    if (!rv(f, alive) || !rv(f, lives) || !rv(f, health)) return false;
    s.alive  = (bool)alive;
    s.lives  = (int)lives;
    s.health = (int)health;
    if (!rv(f, s.pos_x) || !rv(f, s.pos_y)) return false;
    if (!rv(f, s.vel_x) || !rv(f, s.vel_y)) return false;
    if (!rv(f, s.inner_rotation) || !rv(f, s.outer_rotation)) return false;
    if (!rv(f, wave) || !rv(f, difficulty)) return false;
    s.wave = (int)wave; s.difficulty = (int)difficulty;
    if (!rv(f, ships_this_wave) || !rv(f, ships_left)) return false;
    s.ships_this_wave = (int)ships_this_wave; s.ships_left_to_deploy = (int)ships_left;
    if (!rv(f, s.time_until_next_ship)) return false;
    if (!rv(f, deploying) || !rv(f, redeploying)) return false;
    s.deploying = (bool)deploying; s.redeploying = (bool)redeploying;
    uint32_t cnt = 0;
    if (!rv(f, cnt)) return false;
    s.enemies.resize(cnt);
    for (auto &e : s.enemies)
        if (!read_enemy(f, e)) return false;
    return true;
}

// ── Public API ────────────────────────────────────────────────────────────────

bool Save::save_exists() {
    std::string path = save_path();
    if (path.empty()) return false;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    uint32_t magic   = 0;
    uint16_t version = 0;
    bool ok = rv(f, magic) && rv(f, version)
              && magic   == GameState::MAGIC
              && version == GameState::VERSION;
    fclose(f);
    return ok;
}

bool Save::save_game(const Save::GameState &s) {
    std::string path = save_path();
    if (path.empty()) return false;

    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return false;

    bool ok = true;
    uint32_t magic   = GameState::MAGIC;
    uint16_t version = GameState::VERSION;
    ok = ok && wv(f, magic);
    ok = ok && wv(f, version);
    ok = ok && wv(f, (int32_t)s.generation);
    ok = ok && wv(f, s.world_x) && wv(f, s.world_y);
    ok = ok && wv(f, (uint8_t)s.level_cleared);
    ok = ok && wv(f, (int32_t)s.time_until_next_generation);
    ok = ok && wv(f, (int32_t)s.current_time);

    uint32_t cnt;

    cnt = (uint32_t)s.players.size();
    ok = ok && wv(f, cnt);
    for (const auto &p : s.players) ok = ok && write_player(f, p);

    cnt = (uint32_t)s.asteroids.size();
    ok = ok && wv(f, cnt);
    for (const auto &a : s.asteroids) ok = ok && write_asteroid(f, a);

    cnt = (uint32_t)s.pickups.size();
    ok = ok && wv(f, cnt);
    for (const auto &p : s.pickups) ok = ok && write_pickup(f, p);

    cnt = (uint32_t)s.black_holes.size();
    ok = ok && wv(f, cnt);
    for (const auto &bh : s.black_holes) ok = ok && wv(f, bh.pos_x) && wv(f, bh.pos_y);

    ok = ok && write_station(f, s.station);

    fclose(f);

#ifdef __EMSCRIPTEN__
    EM_ASM(
        FS.syncfs(false, function(err) {
            if (err) console.error('[newtonia] IDBFS save failed:', err);
        });
    );
#endif

    return ok;
}

bool Save::load_game(Save::GameState &s) {
    std::string path = save_path();
    if (path.empty()) return false;

    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;

    bool ok = true;

    // Validate header
    uint32_t magic;   if (!rv(f, magic)   || magic   != GameState::MAGIC)   { fclose(f); return false; }
    uint16_t version; if (!rv(f, version) || version != GameState::VERSION)  { fclose(f); return false; }

    int32_t  ival = 0;
    uint8_t  bval = 0;

    ok = ok && rv(f, ival);  s.generation = (int)ival;
    ok = ok && rv(f, s.world_x) && rv(f, s.world_y);
    ok = ok && rv(f, bval);  s.level_cleared = (bool)bval;
    ok = ok && rv(f, ival);  s.time_until_next_generation = (int)ival;
    ok = ok && rv(f, ival);  s.current_time = (int)ival;

    uint32_t cnt = 0;

    ok = ok && rv(f, cnt);
    s.players.resize(cnt);
    for (auto &p : s.players) ok = ok && read_player(f, p);

    ok = ok && rv(f, cnt);
    s.asteroids.resize(cnt);
    for (auto &a : s.asteroids) ok = ok && read_asteroid(f, a);

    ok = ok && rv(f, cnt);
    s.pickups.resize(cnt);
    for (auto &p : s.pickups) ok = ok && read_pickup(f, p);

    ok = ok && rv(f, cnt);
    s.black_holes.resize(cnt);
    for (auto &bh : s.black_holes)
        ok = ok && rv(f, bh.pos_x) && rv(f, bh.pos_y);

    ok = ok && read_station(f, s.station);

    fclose(f);
    return ok;
}

void Save::delete_save() {
    std::string path = save_path();
    if (path.empty()) return;
    std::remove(path.c_str());
#ifdef __EMSCRIPTEN__
    EM_ASM(
        FS.syncfs(false, function(err) {
            if (err) console.error('[newtonia] IDBFS delete failed:', err);
        });
    );
#endif
}
