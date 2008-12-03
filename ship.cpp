#include "ship.h"
#include "point.h"
#include "particle.h"
#include <math.h>

using namespace std;

Ship::Ship(float x, float y) {
  mass = 100.0;
  value = 2000;
  accuracy = 0.1;
  lives = 5;
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
  respawn_time = 5000.0;
  respawns = true;
  shooting = false;
  time_until_next_shot = time_between_shots = 60;
}

void Ship::respawn() {
  position = WrappedPoint(rand(), rand());
  facing = Point(0, 1);
  velocity = Point(0, 0);
  alive = true;
  shooting = false;
  explode();
}

void Ship::kill() {
  if(is_alive()) {
    time_until_respawn = respawn_time;
    lives -= 1;
    alive = false;
    thrusting = false;
    reversing = false;
    rotation_direction = NONE;
    explode();
  }
}

void Ship::kill_stop() {
  if(is_alive()) {
    velocity.zero();
    kill();
  }
}

bool Ship::is_removable() const {
  return false;
}

bool Ship::is_alive() const {
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
  std::vector<Particle>::iterator b = bullets.begin();
  while(b != bullets.end()) {
    if(is_alive() && collide(*b )) {
      kill();
      b = bullets.erase(b);
    } else if(other->is_alive() && other->collide(*b)) {
      other->kill();
      score += other->value;
      b = bullets.erase(b);
    } else {
      b++;
    }
  }
  
  std::vector<Particle>::iterator mine = mines.begin();
  while(mine != mines.end()) {
    if(is_alive() && collide(*mine) || other->is_alive() && other->collide(*mine, 50.0)) {
      detonate(mine->position, mine->velocity);
      mine = mines.erase(mine);
    } else {
      mine++;
    }
  }
}

void Ship::detonate(Point const position, Point const velocity) {
  Point dir = (facing * radius * 1.2);
  for(int i = rand()%50+25; i > 0; i--) {
    dir.rotate(rand()%360*M_PI/180);
    bullets.push_back(Particle(position + dir, velocity + dir*0.0001*(rand()%150), rand()%1000));
  }
}

void Ship::explode() {
  Point dir = (facing * radius * 1.2);
  for(int i = rand()%60+20; i > 0; i--) {
    dir.rotate(rand()%360*M_PI/180);
    debris.push_back(Particle(position + dir, velocity + dir*0.000025*(rand()%300), rand()%3000));
  }
}

/* Circle based collision detection */
bool Ship::collide(Particle const particle, float proximity) const {
  return ((particle.position - position).magnitude_squared() < (radius_squared + proximity*proximity));
}

void Ship::shoot(bool on) {
  shooting = on;
  if(!on) {
    time_until_next_shot = time_between_shots;
  }
}

void Ship::fire_shot() {
  score -= 1;
  Point dir = Point(facing);
  dir.rotate((rand() / (float)RAND_MAX) * accuracy - accuracy / 2.0);
  bullets.push_back(Particle(gun(), dir*0.5 + velocity*0.99, 4000.0));
}

void Ship::mine(bool on) {
  if(!mining && on) {
    lay_mine();
  }
  mining = on;
}

void Ship::lay_mine() {
  score -= 10;
  mines.push_back(Particle(tail(),  facing*-0.1 + velocity*0.1, 30000.0));
}

float Ship::heading() const {
  //FIX: shouldn't have to calculate this each time
  return facing.direction();
}

void Ship::rotate_left(bool on) {
  rotation_direction = on ? LEFT : NONE;
}

void Ship::rotate_right(bool on) {
  rotation_direction = on ? RIGHT : NONE;
}

WrappedPoint Ship::gun() const {
  return WrappedPoint(position + (facing * height * 1.05));
}

WrappedPoint Ship::tail() const {
  return WrappedPoint(position - (facing * 15.0));
}

void Ship::puts() {
  cout << "Facing: " << facing;
  cout << " Position: " << position;
  cout << " Velocity: " << velocity;
  cout << endl;
}

void Ship::step(float delta) {
  if(is_alive()) {
    if(shooting) {
      time_until_next_shot -= delta;
      while(time_until_next_shot <= 0) {
        fire_shot();
        time_until_next_shot += time_between_shots;
      }
    }
  } else if (respawns) {
    time_until_respawn -= delta;
    if(time_until_respawn < 0) {
      respawn();
    }
  }
  
  facing.rotate(rotation_direction * rotation_force / mass  * delta );
  Point acceleration = Point(0,0);
  if(thrusting)
	acceleration += ((facing * thrust_force) / mass);
  if(reversing)
	acceleration += ((facing * reverse_force) / mass);

  velocity += acceleration * delta;
  position += velocity * delta;
  position.wrap();
  
  std::vector<Particle>::iterator b = bullets.begin();
  while(b != bullets.end()) {
    b->step(delta);
    if(!b->is_alive()) {
      b = bullets.erase(b);
    } else {
      b++;
    }
  }
  
  std::vector<Particle>::iterator mine = mines.begin();
  while(mine != mines.end()) {
    mine->step(delta);
    if(!mine->is_alive()) {
      mine = mines.erase(mine);
    } else {
      mine++;
    }
  }
  
  std::vector<Particle>::iterator deb = debris.begin();
  while(deb != debris.end()) {
    deb->step(delta);
    if(!deb->is_alive()) {
      deb = debris.erase(deb);
    } else {
      deb++;
    }
  }
}