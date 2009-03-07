#ifndef SHIP_H
#define SHIP_H

#include "composite_object.h"
#include "point.h"
#include "particle.h"
#include "grid.h"
#include <list>

class Behaviour;
class Object;
namespace Weapon { class Base; }

using namespace std;

class Ship : public CompositeObject {
  public:
    Ship(bool has_friction = true);
    virtual ~Ship();

    void puts(); //TODO: convert into iostream operator

    virtual void step(float delta);

    void rotate_left(bool on = true);
    void rotate_right(bool on = true);
    void thrust(bool on = true);
    void reverse(bool on = true);
    void shoot(bool on = true);
    void shoot_weapon(bool on = true);
    void mine(bool on = true);
    int multiplier() const;

    float heading() const;
    bool is_alive() const;
    virtual bool is_removable() const;

    static void collide(Ship *first, Ship *second);
    // bool collide_object(Object *other);
    void collide_grid(Grid &grid);
    void collide(Ship *other);

    //TODO: make friends with glship
    long long score;
    int lives, kills, kills_this_life;
    //TODO: Make this go away, it's wrong
    float radius_squared;
    bool thrusting, reversing;

    //TODO: make friends with gltrail (or some other way around these public)
    WrappedPoint tail() const;
    Point facing;

    //TODO: somehow get around this public for glstation
    void kill_stop();

    list<Particle> bullets, mines;

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
    void disable_behaviours();
    void disable_weapons();
    
    void next_weapon();
    void previous_weapon();
    WrappedPoint gun() const;
    bool kill();

  protected:

    void lay_mine();
    void respawn(bool was_killed = true);
    void init(bool no_friction);
    virtual void reset(bool was_killed = true);
    void detonate();
    void detonate(Point const position, Point const velocity);

    Point world_size;

    float heat_rate, retro_heat_rate, cool_rate;

    // Forces
    float thrust_force, reverse_force, rotation_force;
    // Attributes
    float width, height, mass;
    long long value;
    // States
    bool mining, respawns, first_life;

    //TODO: encapsulate
    friend class GLStation;
    friend class Enemy;
    friend class GLShip;
    friend class GLEnemy;
    friend class GLGame;
    friend class GLTrail;
    
  private:
    list<Behaviour *> behaviours;
    list<Weapon::Base *> weapons;
};

#endif
