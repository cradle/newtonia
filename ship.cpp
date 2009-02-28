#include "ship.h"
#include "point.h"
#include "particle.h"
#include "asteroid.h"
#include "behaviour.h"
#include "weapon/base.h"
#include "weapon/default.h"
#include <math.h>

using namespace std;

Ship::Ship(bool has_friction) : CompositeObject() {
  alive = false;
  first_life = true;
  score = 0;
  kills = 0;
  position = WrappedPoint();
  init(!has_friction);
}

void Ship::disable_behaviours() {
  while(!behaviours.empty()) {
    delete behaviours.back();
    behaviours.pop_back();
  }
}

void Ship::disable_weapons() {
  while(!weapons.empty()) {
    delete weapons.back();
    weapons.pop_back();
  }
}

Ship::~Ship() {
  disable_behaviours();
} 
   
void Ship::next_weapon() {
  if(weapons.size() != 0) {
    weapons.push_back(weapons.front());
    weapons.pop_front();
  }
}

void Ship::previous_weapon() {
  if(weapons.size() != 0) {
    weapons.push_front(weapons.back());
    weapons.pop_back();
  }
}

void Ship::init(bool no_friction) {
  mass = 100.0;
  value = 1000000;
  lives = 6;
  width = height = radius = 11;
  radius_squared = radius * radius;
  respawn_time = time_until_respawn = 4000;
  max_temperature = 100.0;
  critical_temperature = max_temperature * 0.80;
  explode_temperature = max_temperature * 1.2;
  heat_rate = 0.000;
  retro_heat_rate = heat_rate * -reverse_force / thrust_force;
  cool_rate = retro_heat_rate * 0.9;

  if(no_friction) {
    friction = 0;
    reverse_force = -0.01;
    thrust_force = 0.03;
    rotation_force = 0.3;
  } else {
    friction = 0.001;
    reverse_force = -0.05;
    thrust_force = 0.09;
    rotation_force = 0.2;
  }

  reset();
}

float Ship::temperature_ratio() {
  return temperature/max_temperature;
}

void Ship::respawn(bool was_killed) {
  if(first_life) {
    first_life = false;
    position.wrap();
  } else {
    position = WrappedPoint();
  }
  if(was_killed) {
    lives -= 1;
  }
  alive = true;
  invincible = true;
  time_left_invincible = 1000;
  reset(was_killed);
  detonate();
}

void Ship::reset(bool was_killed) {
  facing = Point(0, 1);
  velocity = Point(0, 0);
  thrusting = false;
  reversing = false;
  rotation_direction = NONE;
  temperature = 0.0;
  disable_behaviours();
  disable_weapons();
  weapons.push_back(new Weapon::Default(this));
  if(was_killed) {
    kills_this_life = 0;
  }
}

void Ship::kill() {
  if(is_alive() && !invincible) {
    alive = false;
    thrusting = false;
    reversing = false;
    rotation_direction = NONE;
    temperature = 0.0;
    time_until_respawn = respawn_time;
    explode();
  }
}

void Ship::kill_stop() {
  if(is_alive() && !invincible) {
    velocity.zero();
    kill();
  }
}

bool Ship::is_removable() const {
  return CompositeObject::is_removable() && (lives == 0) && bullets.empty();
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

int Ship::multiplier() const {
  return kills_this_life / 10 + 1;
}

bool Ship::collide_asteroid(Asteroid* other) {
  std::list<Particle>::iterator b = bullets.begin();
  while(b != bullets.end()) {
    if(other->alive && (*b).collide(other)) {
      (*b).collide(other);
      score += other->get_value() * multiplier();
      kills_this_life += 1;
      kills += 1;
      explode((*b).position, Point(0,0));
      other->kill();
      other->explode();
      bullets.erase(b);
      return true;
    }
    b++;
  }
  std::list<Particle>::iterator mine = mines.begin();
  while(mine != mines.end()) {
    if(other->alive && mine->collide(other, 50.0f)) {
      detonate(mine->position, mine->velocity);
      mine = mines.erase(mine);
    } else {
      mine++;
    }
  }
  if(alive && other->alive && other->collide(this)) {
    detonate();
    kill_stop();
    other->kill();
    return true;
  }
  return false;
}

void Ship::collide(Ship* other) {
  //TODO: Make ships collide with each other too
  std::list<Particle>::iterator b = bullets.begin();
  while(b != bullets.end()) {
    if(is_alive() && b->collide(this)) {
      kill();
      b = bullets.erase(b);
    } else if(other->is_alive() && b->collide(other)) {
      other->kill();
      kills_this_life += 1;
      kills += 1;
      score += other->value * multiplier();
      b = bullets.erase(b);
    } else {
      b++;
    }
  }

  std::list<Particle>::iterator mine = mines.begin();
  while(mine != mines.end()) {
    if(is_alive() && other->is_alive() && mine->collide(other, 50.0)) {
      detonate(mine->position, mine->velocity);
      mine = mines.erase(mine);
    } else {
      mine++;
    }
  }
}

void Ship::detonate() {
  detonate(position, velocity);
}

void Ship::detonate(Point const position, Point const velocity) {
  Point dir = (facing * radius * 1.2);
  for(int i = rand()%50+25; i > 0; i--) {
    dir.rotate(rand()%360*M_PI/180);
    bullets.push_back(Particle(position + dir, velocity + dir*0.0001*(rand()%150), rand()%1000));
  }
}

void Ship::shoot(bool on) {
  if(weapons.size() != 0) {
    weapons.front()->shoot(on);
  }
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
  list<Behaviour *>::iterator vi = behaviours.begin();
  while(vi != behaviours.end()) {
    (*vi)->step(delta);
    if((*vi)->is_done()) {
      delete (*vi);
      vi = behaviours.erase(vi);
    } else {
      vi++;
    }
  }
  
  if(is_alive()) {
    if(invincible) {
      time_left_invincible -= delta;
      if(time_left_invincible < 0) {
        invincible = false;
      }
    }
    
    if(weapons.size() != 0)
      weapons.front()->step(delta);
      
  } else if (lives > 0) {
    time_until_respawn -= delta;
    if(time_until_respawn < 0) {
      respawn();
    }
  }

  facing.rotate(rotation_direction * rotation_force / mass  * delta );
  Point acceleration = Point(0,0);
  if(thrusting) {
  	acceleration += ((facing * thrust_force) / mass);
    temperature += heat_rate * delta;
	}
  if(reversing) {
  	acceleration += ((facing * reverse_force) / mass);
    temperature += retro_heat_rate * delta;
	}
  temperature -= cool_rate * delta;
  if(temperature <= 0)
    temperature = 0;
  if(temperature > explode_temperature && alive) {
    kill();
    detonate();
  }

  velocity += acceleration * delta;
  if(is_alive()) {
    velocity = velocity - velocity * friction * delta;
  }
  CompositeObject::step(delta);

  std::list<Particle>::iterator b = bullets.begin();
  while(b != bullets.end()) {
    b->step(delta);
    if(!b->is_alive()) {
      b = bullets.erase(b);
    } else {
      b++;
    }
  }

  std::list<Particle>::iterator mine = mines.begin();
  while(mine != mines.end()) {
    mine->step(delta);
    if(!mine->is_alive()) {
      mine = mines.erase(mine);
    } else {
      mine++;
    }
  }
}
