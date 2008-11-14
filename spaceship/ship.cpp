#include "ship.h"
#include "point.h"
#include "bullet.h"

using namespace std;

Ship::Ship(float x, float y) {
  mass = 100.0;
  width = height = 10.0;
  thrusting = false;
  thrust_force = 0.02;
  position = WrappedPoint(x, y);
  facing = Point(0, 1);
  velocity = Point(0, 0);
  rotation_force = 0.3;
}

void Ship::thrust(bool on) {
  thrusting = on;
}

void Ship::shoot() {
  Bullet bullet = Bullet(gun(), facing*0.2 + velocity*0.9);
  bullet.set_world_size(world_width, world_height);
  bullets.push_back(bullet);
}

float Ship::heading() {
  //FIX: shouldn't have to calculate this each time
  return facing.direction();
}

void Ship::rotate_left(bool on) {
  rotation_direction = on ? LEFT : NONE;
}

void Ship::rotate_right(bool on) {
  rotation_direction = on ? RIGHT : NONE;
}

Point Ship::gun() {
  return position + (facing * height);
}

void Ship::puts() {
  cout << "Facing: " << facing;
  cout << " Position: " << position;
  cout << " Velocity: " << velocity;
  cout << endl;
}

void Ship::set_world_size(float world_width, float world_height) {
  this->world_width = world_width;
  this->world_height = world_height;
  
  position.set_boundaries(-(world_width/2 + width), world_height/2 + height,
                            world_width/2 + width, -(world_height/2 + height));

  for(vector<Bullet>::iterator bullet = bullets.begin(); bullet != bullets.end(); bullet++) {
    bullet->set_world_size(width, height);
  }
}

void Ship::step(float delta) {
  // TODO: Move to acceleration/force based rotation
  // Step physics
  facing.rotate(rotation_direction * rotation_force / mass  * delta );
  if(thrusting)
    velocity += ((facing * thrust_force) / mass) * delta;
  position += velocity * delta;
  position.wrap();
  
  // Step bullets
  for(vector<Bullet>::iterator bullet = bullets.begin(); bullet != bullets.end(); bullet++) {
    bullet->step(delta);
  }
  
}
