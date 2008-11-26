#include "ship.h"
#include "point.h"
#include "bullet.h"

using namespace std;

Ship::Ship(float x, float y) {
  mass = 100.0;
  width = height = radius = 10.0;
  thrusting = false;
  thrust_force = 0.02;
  reversing = false;
  reverse_force = -0.01;
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

void Ship::reverse(bool on) {
  reversing = on;
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
  
  std::vector<Bullet>::iterator mine = mines.begin();
  while(mine != mines.end()) {
    if(collide(*mine)) {
      kill();
      score -= 1;
      bullet = mines.erase(mine);
    } else if(other->collide(*mine)) {
      other->kill();
      score += 1;
      bullet = mines.erase(mine);
    } else {
      mine++;
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
  bullets.push_back(Bullet(gun(), facing*0.5 + velocity*0.99, 2000.0));
}

void Ship::lay_mine() {
  mines.push_back(Bullet(tail(),  facing*-0.1 + velocity*0.95, 30000.0));
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
  return position + (facing * height * 1.05);
}

Point Ship::tail() {
  return position - (facing * 15.0);
}

void Ship::puts() {
  cout << "Facing: " << facing;
  cout << " Position: " << position;
  cout << " Velocity: " << velocity;
  cout << endl;
}

void Ship::step(float delta) {
  std::vector<Bullet>::iterator bullet = bullets.begin();
  while(bullet != bullets.end()) {
    bullet->step(delta);
    bullet = bullet->is_alive() ? bullet+1 : bullets.erase(bullet);
  }
  
  std::vector<Bullet>::iterator mine = mines.begin();
  while(mine != mines.end()) {
    mine->step(delta);
    mine = mine->is_alive() ? mine+1 : mines.erase(mine);
  }
  
  facing.rotate(rotation_direction * rotation_force / mass  * delta );
  acceleration = Point(0,0);
  if(thrusting)
    acceleration += ((facing * thrust_force) / mass);
  if(reversing)
    acceleration += ((facing * reverse_force) / mass);
    
  velocity += acceleration * delta;
  position += velocity * delta;
  position.wrap();
}
