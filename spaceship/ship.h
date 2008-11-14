#include "point.h"
#include "bullet.h"
#include <vector>

class Ship {
  public:
    Ship(float x, float y);
    
    void rotate_left(bool on = true);
    void rotate_right(bool on = true);
    void thrust(bool on = true);
    void shoot();
    
    // TODO: make 'friend' with some sort of VIEW
    bool thrusting;
    Point position;
    float width;
    float height;
    float heading();
    
    // Step moves the engine forward delta seconds, zeroes forces
    void step(float delta);
    
    void puts();

  private:
    enum Rotation { 
      LEFT = 1, 
      NONE = 0, 
      RIGHT = -1 
    };

    float mass;

    // Linear
    float thrust_force;
    Point velocity;
    
    // Angular
    float rotation_force;
    Point facing;
    Rotation rotation_direction;
    
    // Projectiles
    std::vector<Bullet> bullets;
};
