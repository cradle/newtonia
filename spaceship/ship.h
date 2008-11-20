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
    void shoot();

    // TODO: make 'friend' with some sort of VIEW
    bool thrusting;
    WrappedPoint position;
    float width, height, radius;
    float heading();
    void kill();
    bool is_alive();

    // Step moves the engine forward delta seconds, zeroes forces
    virtual void step(float delta);

    void puts();

    // Projectiles
    //TODO: friends
    std::vector<Bullet> bullets;

    static void collide(Ship* first, Ship* second);

    void collide(Ship* other);
    bool collide(Bullet bullet);

    int score;
    enum Rotation {
      LEFT = 1,
      NONE = 0,
      RIGHT = -1
    };
    Rotation rotation_direction;
    
    
    Point tail();

    // Linear
    float thrust_force;
    Point velocity;

    // Angular
    float rotation_force;
    Point facing;


    Point world_size;

  protected:
    bool alive;
    float mass;

    Point gun();
};

#endif
