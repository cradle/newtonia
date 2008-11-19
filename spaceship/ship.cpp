#include "ship.h"
#include "point.h"
#include "bullet.h"

using namespace std;

Ship::Ship(float x, float y) {
  mass = 100.0;
  width = height = radius = 10.0;
  thrusting = false;
  thrust_force = 0.02;
  position = WrappedPoint(x, y);
  facing = Point(0, 1);
  velocity = Point(0, 0);
  rotation_force = 0.3;
  alive = true;
  score = 0;
  rotation_direction = NONE;
}

void Ship::kill() {
  alive = false;
  thrusting = false;
  rotation_direction = NONE;
}

bool Ship::is_alive() {
  return alive;
}

void Ship::thrust(bool on) {
  thrusting = on;
}

void Ship::collide(Ship* first, Ship* second) {
  // first.collide(second);
  // second.collide(first);
  //TODO: DRYness
  first->collide(second);
  second->collide(first);
}

void Ship::collide(Ship* other) {
  //TODO: Make ships collide with each other too
  std::vector<Bullet>::iterator bullet = bullets.begin();
  while(bullet != bullets.end()) {
    if(collide(*bullet)) {
      kill();
      score -= 1;
      bullet = bullets.erase(bullet);
    } else if(other->collide(*bullet)) {
      other->kill();
      score += 1;
      bullet = bullets.erase(bullet);
    } else {
      bullet++;
    }
  }
}

bool Ship::collide(Bullet bullet) { // Circle based collision
  //TODO: Make more accurate. Doesn't currently reflect shape of ship at all
  return ((bullet.position - position).magnitude() < radius);
}
/*
bool Ship::collide_square(Bullet bullet) {
  return (bullet.position.x > (position.x - width) && \
          bullet.position.x < (position.x + width) && \
          bullet.position.y > (position.y - height) && \
          bullet.position.y < (position.y + height));
}*/

void Ship::shoot() {
  Bullet bullet = Bullet(gun(), facing*0.2 + velocity*0.9);
  bullet.set_world_size(world_size);
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

void Ship::set_world_size(Point size) {
  //TODO: world should not be (-width, width), should be (0, width)
  world_size = size;

  position.set_boundaries(size);

  for(vector<Bullet>::iterator b = bullets.begin(); b != bullets.end(); b++) {
    b->set_world_size(size);
  }
}

void Ship::step(float delta) {
  // TODO: fix timestep, decouple timestep
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
