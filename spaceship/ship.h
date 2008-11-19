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

    void set_world_size(float width, float height);

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


  protected:
    bool alive;
    float mass;
    float world_width, world_height;

    Point gun();

    // Linear
    float thrust_force;
    Point velocity;

    // Angular
    float rotation_force;
    Point facing;
};

#endif
