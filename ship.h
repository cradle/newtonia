#ifndef SHIP_H
#define SHIP_H

#include "point.h"
#include "particle.h"
#include <list>

using namespace std;

class Ship {
  public:
    Ship() {};
    Ship(float x, float y);
    virtual ~Ship() {};
    
    void puts(); //TODO: convert into iostream operator

    virtual void step(float delta);

    void rotate_left(bool on = true);
    void rotate_right(bool on = true);
    void thrust(bool on = true);
    void reverse(bool on = true);
    void shoot(bool on = true);
    void mine(bool on = true);

    float heading() const;
    bool is_alive() const;
    virtual bool is_removable() const;

    static void collide(Ship* first, Ship* second);    
    void collide(Ship* other);
    bool collide(Particle const particle, float proximity = 0) const;

    //TODO: make friends with glship
    int score, lives;
    float radius, radius_squared;
    bool thrusting, reversing;
    WrappedPoint position;
    
    //TODO: make friends with gltrail (or some other way around these public)
    WrappedPoint tail() const;
    Point facing;
    Point velocity;
    
    //TODO: somehow get around this public for glstation
    void kill_stop();
    
    list<Particle> bullets, mines, debris;
    
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

  protected:
    WrappedPoint gun() const;
    
    void fire_shot();
    void lay_mine();
    void explode();
    void respawn();
    void detonate(Point const position, Point const velocity);
    void kill();

    Point world_size;
    
    float heat_rate, retro_heat_rate, cool_rate;
    
    // Forces
    float thrust_force, reverse_force, rotation_force;
    // Attributes
    float width, height, mass, accuracy, value;
    // States
    bool shooting, mining, alive, respawns, first_life;
    
    friend class GLShip;
};

#endif
