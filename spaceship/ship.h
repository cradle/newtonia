#ifndef SHIP_H
#define SHIP_H

#include "point.h"
#include "particle.h"
#include <vector>

class Ship {
  public:
    Ship() {};
    Ship(float x, float y);
    virtual ~Ship() {};

    void set_world_size(Point world_size);

    void rotate_left(bool on = true);
    void rotate_right(bool on = true);
    void thrust(bool on = true);
    void reverse(bool on = true);
    void shoot(bool on = true);
    void fire_shot();
    void lay_mine();

    // TODO: make 'friend' with some sort of VIEW
    bool thrusting;
    bool reversing;
    WrappedPoint position;
    float width, height, radius, radius_squared;
    float heading();
    void kill();
    bool is_alive();
    virtual bool is_removable();
    void explode();
    void respawn();
    void detonate(Point position, Point velocity);

    // Step moves the engine forward delta seconds, zeroes forces
    virtual void step(float delta);

    void puts();

    // Projectiles
    //TODO: friends
    std::vector<Particle> bullets;
    std::vector<Particle> mines;
    std::vector<Particle> debris;

    static void collide(Ship* first, Ship* second);

    void collide(Ship* other);
    bool collide(Particle particle, float proximity = 0);

    int score, value;
    enum Rotation {
      LEFT = 1,
      NONE = 0,
      RIGHT = -1
    };
    Rotation rotation_direction;
    
    
    WrappedPoint tail();

    // Linear
    float thrust_force;
    float reverse_force;
    Point velocity;
    Point acceleration;

    // Angular
    float rotation_force;
    Point facing;


    Point world_size;

  protected:
    bool alive;
    float mass;
    float respawn_time, time_until_respawn;
    float accuracy;
    float time_until_next_shot, time_between_shots;
    bool shooting;
    bool respawns;

    WrappedPoint gun();
};

#endif
