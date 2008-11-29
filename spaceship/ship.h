#ifndef SHIP_H
#define SHIP_H

#include "point.h"
#include "bullet.h"
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
    void shoot();
    void lay_mine();

    // TODO: make 'friend' with some sort of VIEW
    bool thrusting;
    bool reversing;
    WrappedPoint position;
    float width, height, radius, radius_squared;
    float heading();
    void kill();
    bool is_alive();
    bool is_removable();
    void explode();
    void detonate(Point position, Point velocity);

    // Step moves the engine forward delta seconds, zeroes forces
    virtual void step(float delta);

    void puts();

    // Projectiles
    //TODO: friends
    std::vector<Bullet> bullets;
    std::vector<Bullet> mines;
    std::vector<Bullet> debris;

    static void collide(Ship* first, Ship* second);

    void collide(Ship* other);
    bool collide(Bullet bullet, float proximity = 0);

    int score;
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

    WrappedPoint gun();
};

#endif
