#include "ship.h"
#include "point.h"
#include "particle.h"
#include "asteroid.h"
#include "behaviour.h"
#include "weapon/base.h"
#include "weapon/default.h"
#include "weapon/mine.h"
#include <math.h>
#include <iostream>

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
  while(!primary_weapons.empty()) {
    delete primary_weapons.back();
    primary_weapons.pop_back();
  }
  while(!secondary_weapons.empty()) {
    delete secondary_weapons.back();
    secondary_weapons.pop_back();
  }
}

Ship::~Ship() {
  disable_weapons();
  disable_behaviours();
}

void Ship::next_weapon() {
  list<Weapon::Base *>::iterator next(primary);

  //TODO: use circular list?
  next++;

  if(next == primary_weapons.end())
    next = primary_weapons.begin();

  (*next)->shoot((*primary)->is_shooting());
  (*primary)->shoot(false);
  primary = next;
}

void Ship::previous_weapon() {
  list<Weapon::Base *>::iterator next;

  //TODO: use circular list?
  if(next == primary_weapons.begin())
    next = primary_weapons.end();

  next--;

  (*next)->shoot((*primary)->is_shooting());
  (*primary)->shoot(false);
  primary = next;
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
  boost_heat = 0.000;
  boost_force = 2.0;
  boosting = false;

  if(no_friction) {
    friction = 0;
    reverse_force = -0.01;
    thrust_force = 0.03;
    rotation_force = 0.5;
  } else {
    friction = 0.001;
    reverse_force = -0.05;
    thrust_force = 0.09;
    rotation_force = 0.4;
  }

   primary_weapons.push_front(new Weapon::Default(this, false, 1));
   primary_weapons.push_front(new Weapon::Default(this, false, 2));
   primary_weapons.push_front(new Weapon::Default(this, false, 3));
   primary_weapons.push_front(new Weapon::Default(this, false, 4));
   primary_weapons.push_front(new Weapon::Default(this, true));
   primary_weapons.push_front(new Weapon::Default(this, true, 1));
   primary_weapons.push_front(new Weapon::Default(this, true, 2));
   primary_weapons.push_front(new Weapon::Default(this, true, 3));
   primary_weapons.push_front(new Weapon::Default(this, true, 4));

  primary_weapons.push_front(new Weapon::Default(this));
  primary = primary_weapons.begin();

  secondary_weapons.push_front(new Weapon::Mine(this));
  secondary = secondary_weapons.begin();

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
  time_left_invincible = 1500;
  reset(was_killed);
  detonate();
}

void Ship::reset(bool was_killed) {
  facing = Point(0, 1);
  velocity = Point(0, 0);
  thrusting = false;
  reversing = false;
  shoot(false);
  mines.clear();
  bullets.clear();
  debris.clear();
  rotation_direction = NONE;
  still_rotating_left = false;
  still_rotating_right = false;
  temperature = 0.0;
  if(was_killed) {
    kills_this_life = 0;
  }
}

bool Ship::kill() {
  if(CompositeObject::kill()) {
    thrusting = false;
    reversing = false;
    rotation_direction = NONE;
    temperature = 0.0;
    time_until_respawn = respawn_time;
    return true;
  }
  return false;
}

void Ship::kill_stop() {
  if(kill()) {
    velocity.zero();
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

void Ship::boost() {
  boosting = true;
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

void Ship::collide_grid(Grid &grid) {
  Object * object;

  if(alive) {
    object = grid.collide(*this);
    if(object != NULL) {
      if(object->kill()) {
        detonate();
      } else {
        explode(position, object->velocity);
      }
      kill_stop();
    }
  }

  std::list<Particle>::iterator p = mines.begin();
  while(p != mines.end()) {
    object = grid.collide(*p, 50.0f);
    if(object != NULL && object->alive) {
      detonate(p->position, p->velocity, 50);
      p = mines.erase(p);
    } else {
      p++;
    }
  }

  p = bullets.begin();
  while(p != bullets.end()) {
    object = grid.collide(*p);
    if(object != NULL) {
      if(object->kill()) {
        score += object->get_value() * multiplier();
        kills_this_life += 1;
        kills += 1;
      }
      explode((*p).position, object->velocity);
      p = bullets.erase(p);
    } else {
      p++;
    }
  }
}
//
// bool Ship::collide_object(Object* other) {
//   std::list<Particle>::iterator b = bullets.begin();
//   while(b != bullets.end()) {
//     if(other->alive && (*b).collide(*other)) {
//       explode((*b).position, Point(0,0));
//       bullets.erase(b);
//       if(other->kill()) {
//         score += other->get_value() * multiplier();
//         kills_this_life += 1;
//         kills += 1;
//         // other->explode();
//         return true;
//       }
//     }
//     b++;
//   }
//   std::list<Particle>::iterator mine = mines.begin();
//   while(mine != mines.end()) {
//     if(other->alive && mine->collide(other, 50.0f)) {
//       detonate(mine->position, mine->velocity);
//       mine = mines.erase(mine);
//     } else {
//       mine++;
//     }
//   }
//   if(alive && other->alive && other->collide(this)) {
//     if(!invincible) {
//       detonate();
//     }
//     kill_stop();
//     if(other->kill())
//       return true;
//   }
//   return false;
// }

void Ship::collide(Ship *other) {
  std::list<Particle>::iterator b = bullets.begin();
  while(b != bullets.end()) {
    if(other->is_alive() && b->collide(*other)) {
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
    if(is_alive() && other->is_alive() && mine->collide(*other, 50.0)) {
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

void Ship::detonate(Point const position, Point const velocity, int particle_count) {
  Point dir = (facing * radius * 1.2);
  for(int i = rand()%particle_count+particle_count/2; i > 0; i--) {
    dir.rotate(rand()%360*M_PI/180);
    bullets.push_back(Particle(position + dir, velocity + dir*0.0001*(rand()%150), rand()%1500));
  }
}

void Ship::shoot(bool on) {
  if(!primary_weapons.empty()) {
    if((*primary)->empty() && on) {
      next_weapon();
    } else {
      (*primary)->shoot(on);
    }
  }
}

void Ship::mine(bool on) {
  (*secondary)->shoot(on);
  if((*secondary)->empty() && on) {
    //TODO: allow 'empty' secondary when needed
    //delete *secondary;
    //secondary = secondary_weapons.erase(secondary);
  }
}

float Ship::heading() const {
  //FIX: shouldn't have to calculate this each time
  return facing.direction();
}

void Ship::rotate_left(bool on) {
  still_rotating_left = on;
  if(on) {
    rotation_direction = LEFT;
  } else if (still_rotating_right) {
    rotation_direction = RIGHT;
  } else {
    rotation_direction = NONE;
  }
}

void Ship::rotate_right(bool on) {
  still_rotating_right = on;
  if(on) {
    rotation_direction = RIGHT;
  } else if (still_rotating_left) {
    rotation_direction = LEFT;
  } else {
    rotation_direction = NONE;
  }
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

    (*primary)->step(delta);

  } else if (lives > 0) {
    time_until_respawn -= delta;
    if(time_until_respawn < 0) {
      respawn();
    }
  }

  facing.rotate(rotation_direction * rotation_force / mass  * delta );
  Point acceleration = Point(0,0);
  if(boosting) {
  	acceleration += ((facing * boost_force) / mass);
    temperature += boost_heat;
    explode();
    boosting = false;
    }
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
