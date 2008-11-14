#include "ship.h"
#include "point.h"
#include "bullet.h"

using namespace std;

Ship::Ship(float x, float y) {
  mass = 100.0;
  width = height = 10.0;
  thrusting = false;
  thrust_force = 0.02;
  position = Point(x, y);
  facing = Point(0, 1);
  velocity = Point(0, 0);
  rotation_force = 0.3;
}

void Ship::thrust(bool on) {
  thrusting = on;
}

void Ship::shoot() {
  bullets.push_back(Bullet(position + (facing * (width/2)), facing));
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

void Ship::puts() {
  cout << "Facing: " << facing;
  cout << " Position: " << position;
  cout << " Velocity: " << velocity;
  cout << endl;
}

void Ship::step(float delta) {
  // TODO: Move to acceleration/force based rotation
  // Step physics
  facing.rotate(rotation_direction * rotation_force / mass  * delta );
  if(thrusting)
    velocity += ((facing * thrust_force) / mass) * delta;
  position += velocity * delta;
  
  // Step bullets
  for(vector<Bullet>::iterator bullet = bullets.begin(); bullet != bullets.end(); bullet++) {
    bullet->step(delta);
  }
  
}
