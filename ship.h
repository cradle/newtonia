#ifndef SHIP_H
#define SHIP_H

#include "composite_object.h"
#include "point.h"
#include "particle.h"
#include "behaviour.h"
#include <list>

class Asteroid;

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
    void mine(bool on = true);
    int multiplier() const;

    float heading() const;
    bool is_alive() const;
    virtual bool is_removable() const;

    static void collide(Ship *first, Ship *second);
    bool collide_asteroid(Asteroid *other);
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

    // Heat
    float temperature_ratio();
    float max_temperature, critical_temperature, temperature, explode_temperature;

    // I need friends for views
    // Timings
    int respawn_time, time_until_respawn;
    int time_until_next_shot, time_between_shots;

    //FIX: friends
    int time_left_invincible;
    bool invincible;
    void disable_behaviours();

  protected:
    WrappedPoint gun() const;

    void fire_shot();
    void lay_mine();
    void respawn(bool was_killed = true);
    void init(bool no_friction);
    virtual void reset(bool was_killed = true);
    void detonate();
    void detonate(Point const position, Point const velocity);
    void kill();

    Point world_size;

    float heat_rate, retro_heat_rate, cool_rate;

    // Forces
    float thrust_force, reverse_force, rotation_force;
    // Attributes
    float width, height, mass, accuracy;
    long long value;
    // States
    bool shooting, mining, alive, respawns, first_life, automatic_fire;

    //TODO: encapsulate
    friend class GLStation;
    friend class Enemy;
    friend class GLShip;
    friend class GLGame;
    friend class GLTrail;
    
  private:
    list<Behaviour *> behaviours;
};

#endif
