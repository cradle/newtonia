#include "ship.h"
#include "point.h"
#include "bullet.h"
#include <math.h>

using namespace std;

Ship::Ship(float x, float y) {
  mass = 100.0;
  width = height = radius = 10.0;
  radius_squared = radius*radius;
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
  reversing = false;
  rotation_direction = NONE;
  explode();
}

bool Ship::is_removable() {
  return false;//!alive && bullets.empty();
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
    if(is_alive() && collide(*bullet)) {
      kill();
      score -= 1;
      bullet = bullets.erase(bullet);
    } else if(other->is_alive() && other->collide(*bullet)) {
      other->kill();
      score += 1;
      bullet = bullets.erase(bullet);
    } else {
      bullet++;
    }
  }
  
  std::vector<Bullet>::iterator mine = mines.begin();
  while(mine != mines.end()) {
    if(is_alive() && collide(*mine) || other->is_alive() && other->collide(*mine, 50.0)) {
      detonate(mine->position, mine->velocity);
      mine = mines.erase(mine);
    } else {
      mine++;
    }
  }
}

void Ship::detonate(Point position, Point velocity) {
  Point dir = (facing * radius * 1.2);
  for(int i = rand()%100+50; i > 0; i--) {
    dir.rotate(rand()%360*M_PI/180);
    bullets.push_back(Bullet(position + dir, velocity + dir*0.0001*(rand()%200), rand()%1000));
  }
}

void Ship::explode() {
  Point dir = (facing * radius * 1.2);
  for(int i = rand()%100+50; i > 0; i--) {
    dir.rotate(rand()%360*M_PI/180);
    debris.push_back(Bullet(position + dir, velocity + dir*0.0001*(rand()%300), rand()%4000));
  }
}

bool Ship::collide(Bullet bullet, float proximity) { // Circle based collision
  //TODO: Make more accurate. Doesn't currently reflect shape of ship at all
  return ((bullet.position - position).magnitude_squared() < (radius_squared + proximity*proximity));
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

WrappedPoint Ship::gun() {
  return WrappedPoint(position + (facing * height * 1.05));
}

WrappedPoint Ship::tail() {
  return WrappedPoint(position - (facing * 15.0));
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
    if(!bullet->is_alive()) {
      bullet = bullets.erase(bullet);
    } else {
      bullet++;
    }
  }
  
  std::vector<Bullet>::iterator mine = mines.begin();
  while(mine != mines.end()) {
    mine->step(delta);
    if(!mine->is_alive()) {
      mine = mines.erase(mine);
    } else {
      mine++;
    }
  }
  
  std::vector<Bullet>::iterator deb = debris.begin();
  while(deb != debris.end()) {
    deb->step(delta);
    if(!deb->is_alive()) {
      deb = debris.erase(deb);
    } else {
      deb++;
    }
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
