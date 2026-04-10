#ifndef SHIP_H
#define SHIP_H

#include "composite_object.h"
#include "point.h"
#include "particle.h"
#include "weapon/missile.h"
#include "grid.h"
#include "black_hole.h"
#include "savegame.h"
#include <list>
#include <vector>
#include <SDL.h>
#include <SDL_mixer.h>

struct Shockwave {
  Point position;
  float radius;
  float prev_radius;
  float max_radius;
  float speed;        // units per ms
  float time_left;   // ms

  Shockwave(Point pos, float max_r, float spd, float duration)
    : position(pos), radius(0.0f), prev_radius(0.0f),
      max_radius(max_r), speed(spd), time_left(duration) {}

  bool alive() const { return time_left > 0.0f && radius < max_radius; }

  void step(float delta) {
    prev_radius = radius;
    radius += speed * delta;
    time_left -= delta;
    if(radius > max_radius) radius = max_radius;
  }
};

class Behaviour;
class Object;
namespace Weapon { class Base; }

using namespace std;

class Ship : public CompositeObject {
  public:
    Ship(const Grid &grid, bool has_friction = true);
    virtual ~Ship();

    void puts(); //TODO: convert into iostream operator

    using CompositeObject::step;
    virtual void step(float delta, const Grid &grid);

    void rotate_left(bool on = true);
    void rotate_right(bool on = true);
    void thrust(bool on = true);
    void reverse(bool on = true);
    void shoot(bool on = true);
    void shoot_weapon(bool on = true);
    void fire_secondary(bool on = true);
    void boost();
    int multiplier() const;

    float heading() const;
    bool is_alive() const;
    virtual bool is_removable() const;

    using Object::collide;
    static void collide(Ship *first, Ship *second);
    // bool collide_object(Object *other);
    void collide_grid(Grid &grid, int delta);
    void collide(Ship *other);

    //TODO: make friends with glship
    int score;
    int lives, kills, kills_this_life;
    //TODO: Make this go away, it's wrong
    float radius_squared;
    bool thrusting, reversing, boosting;
    float sound_volume_scale = 1.0f;  // 0=silent, 1=full; set by GLGame for enemy AI

    // Analog scale factors (0.0–1.0); set by joystick/controller input
    float rotation_scale;  // scales rotation_force (default 1.0)
    float thrust_analog;   // scales thrust_force   (default 1.0)
    float reverse_analog;  // scales reverse_force  (default 1.0)

    //TODO: make friends with gltrail (or some other way around these public)
    WrappedPoint tail() const;
    Point facing;

    //TODO: somehow get around this public for glstation
    void kill_stop();

    std::vector<Particle> bullets, mines, giga_mines, bullet_trails;
    std::vector<MissileShot> missiles;
    std::vector<Shockwave> shockwaves;

    enum Rotation {
      LEFT = 1,
      NONE = 0,
      RIGHT = -1
    };
    Rotation rotation_direction;
    bool still_rotating_left, still_rotating_right;

    // Heat
    float temperature_ratio();
    float max_temperature, critical_temperature, temperature, explode_temperature;

    // I need friends for views
    // Timings
    int respawn_time, time_until_respawn;

    //FIX: friends
    int time_left_invincible;
    // Serialisation: capture/restore the full player state including weapons.
    Save::Player capture_state() const;
    void restore_state(const Save::Player &p, const Grid &grid);

    void add_behaviour(Behaviour *b);
    void disable_behaviours();
    void disable_weapons();

    void next_weapon();
    void previous_weapon();
    void next_secondary_weapon();
    void add_weapon(int weapon_index);
    void add_mine_ammo(int amount);
    void add_giga_mine_ammo(int amount);
    void add_missile_ammo(int amount);
    void add_shield_ammo(int amount);
    void add_god_mode(int duration_ms = 10000);
    int god_mode_time_remaining() const;
    void add_nova_ammo(int amount);
    void nova_detonate();
    int nova_ammo() const;
    void set_shield_hum(bool on);
    void set_missile_asteroids(std::list<Object*> *asteroids);
    void set_missile_ships(std::list<Object*> *ships);
    void set_black_holes(const std::list<BlackHole*> *bhs);
    WrappedPoint gun() const;
    void mark_last_bullet_trail();
    void mark_last_bullet_kills_invincible();
    void fire_bullet_from_gun();
    bool kill();

  protected:

    void lay_mine();
    void respawn(const Grid &grid, bool was_killed = true);
    void init(bool no_friction);
    virtual void reset(bool was_killed = true);
    void detonate();
    void detonate(Point const position, Point const velocity, int particle_count = 10);
    void giga_detonate(Point const position);

    Point world_size;

    float heat_rate, retro_heat_rate, cool_rate, boost_heat;

    // Forces
    float thrust_force, reverse_force, rotation_force, boost_force;
    // Attributes
    float width, height, mass;
    int value;
    // States
    bool mining, respawns, first_life, toggled;

    //TODO: encapsulate
    friend class GLStation;
    friend class Enemy;
    friend class GLShip;
    friend class GLEnemy;
    friend class GLGame;
    friend class GLTrail;

  private:
    void safe_position(const Grid &grid, bool try_current = false);

    void play_rotating_sound(bool on);
    void update_god_mode_music(int time_remaining);
    void stop_god_mode_music();
    Mix_Chunk *boost_sound = NULL, *tic_sound = NULL, *tic_low_sound = NULL, *click_sound = NULL;
    Mix_Chunk *missile_explode_sound = NULL, *shield_hum_sound = NULL, *explode_sound = NULL;
    Mix_Chunk *giga_mine_explode_sound = NULL, *mine_explode_sound = NULL;
    Mix_Chunk *shoot_sound = NULL;
    Mix_Chunk *god_mode_music_sound = NULL, *god_mode_music_warn_sound = NULL;
    int shield_hum_channel = -1;
    int boost_channel = -1;
    int god_mode_music_channel = -1;
    int god_mode_music_phase = 0;  // 0=off, 1=main, 2=warn

    list<Behaviour *> behaviours;
    list<Weapon::Base *> primary_weapons;
    list<Weapon::Base *> secondary_weapons;
    list<Weapon::Base *>::iterator primary;
    list<Weapon::Base *>::iterator secondary;
    std::list<Object*> *missile_asteroids = nullptr;
    const std::list<BlackHole*> *black_holes = nullptr;
    std::list<Object*> *missile_ships_list = nullptr;
};

#endif
