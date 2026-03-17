#include "ship.h"
#include "point.h"
#include "particle.h"
#include "asteroid.h"
#include "behaviour.h"
#include "weapon/base.h"
#include "weapon/default.h"
#include "weapon/mine.h"
#include "weapon/missile.h"
#include "weapon/shield.h"
#include <math.h>
#include <iostream>

using namespace std;

Ship::Ship(const Grid &grid, bool has_friction) :
    CompositeObject(),
    toggled(false) {
  alive = false;
  first_life = true;
  score = 0;
  kills = 0;
  position = WrappedPoint();
  safe_position(grid);
  init(!has_friction);
  if(tic_sound == NULL) {
    tic_sound = Mix_LoadWAV("audio/tic.wav");
    if(tic_sound == NULL) {
      std::cout << "Unable to load tic.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(tic_low_sound == NULL) {
    tic_low_sound = Mix_LoadWAV("audio/tic_low.wav");
    if(tic_low_sound == NULL) {
      std::cout << "Unable to load tic_low.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(click_sound == NULL) {
    click_sound = Mix_LoadWAV("audio/click.wav");
    if(click_sound == NULL) {
      std::cout << "Unable to load click.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(boost_sound == NULL) {
    boost_sound = Mix_LoadWAV("audio/boost.wav");
  }
  if(boost_sound != NULL) {
    Mix_VolumeChunk(boost_sound, 0);
    Mix_PlayChannel(-1, boost_sound, -1);
  } else {
    std::cout << "Unable to load boost.wav (" << Mix_GetError() << ")" << std::endl;
  }
  if(missile_explode_sound == NULL) {
    missile_explode_sound = Mix_LoadWAV("audio/missile_explode.wav");
    if(missile_explode_sound == NULL) {
      std::cout << "Unable to load missile_explode.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
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
  if(tic_sound != NULL) {
    Mix_FreeChunk(tic_sound);
  }
  if(tic_low_sound != NULL) {
    Mix_FreeChunk(tic_low_sound);
  }
  if(boost_sound != NULL) {
    Mix_FreeChunk(boost_sound);
  }
  if(click_sound != NULL) {
    Mix_FreeChunk(click_sound);
  }
  if(missile_explode_sound != NULL) {
    Mix_FreeChunk(missile_explode_sound);
  }
}

void Ship::next_weapon() {
  if(click_sound != NULL) {
    Mix_PlayChannel(-1, click_sound, 0);
  }
  list<Weapon::Base *>::iterator next(primary);

  //TODO: delete exhausted
  //TODO: use circular list?
  next++;

  if(next == primary_weapons.end())
    next = primary_weapons.begin();

  (*next)->shoot((*primary)->is_shooting());
  (*primary)->shoot(false);

  primary = next;
}

void Ship::previous_weapon() {
  if(click_sound != NULL) {
    Mix_PlayChannel(-1, click_sound, 0);
  }
  list<Weapon::Base *>::iterator next(primary);

  //TODO: use circular list?
  if(next == primary_weapons.begin())
    next = primary_weapons.end();

  next--;

  (*next)->shoot((*primary)->is_shooting());
  (*primary)->shoot(false);
  primary = next;
}

void Ship::next_secondary_weapon() {
  if(secondary_weapons.empty()) return;
  if(click_sound != NULL) {
    Mix_PlayChannel(-1, click_sound, 0);
  }
  list<Weapon::Base *>::iterator next(secondary);
  next++;
  if(next == secondary_weapons.end())
    next = secondary_weapons.begin();
  (*secondary)->shoot(false);
  secondary = next;
}

struct WeaponConfig {
  bool automatic;
  int level;
  float accuracy;
  int time_between_shots;
};

static const WeaponConfig weapon_configs[] = {
  { false, 1, 0.1f, 100 },   // 0
  { false, 2, 0.1f, 100 },   // 1
  { false, 3, 0.1f, 100 },   // 2
  { false, 4, 0.1f, 100 },   // 3
  { false, 5, 0.1f, 100 },   // 4
  { false, 0, 0.1f,  50 },   // 5
  { false, 0, 0.0f, 200 },   // 6
  { true,  0, 0.1f, 100 },   // 7
  { true,  1, 0.1f, 100 },   // 8
  { true,  2, 0.1f, 100 },   // 9
  { true,  3, 0.1f, 100 },   // 10
  { true,  4, 0.1f, 100 },   // 11
  { true,  5, 0.1f, 100 },   // 12
  { true,  0, 0.1f,  50 },   // 13
  { true,  0, 0.0f, 200 },   // 14
};

static const int num_weapon_configs = sizeof(weapon_configs) / sizeof(weapon_configs[0]);

void Ship::add_weapon(int weapon_index) {
  if(weapon_index < 0 || weapon_index >= num_weapon_configs) return;
  const WeaponConfig &cfg = weapon_configs[weapon_index];

  for(auto it = primary_weapons.begin(); it != primary_weapons.end(); ++it) {
    Weapon::Default *w = dynamic_cast<Weapon::Default*>(*it);
    if(w && w->weapon_index() == weapon_index) {
      w->add_ammo(100);
      (*primary)->shoot(false);
      primary_weapons.splice(primary_weapons.end(), primary_weapons, it);
      primary = --primary_weapons.end();
      return;
    }
  }

  primary_weapons.push_back(new Weapon::Default(this, cfg.automatic, cfg.level, cfg.accuracy, cfg.time_between_shots, weapon_index));
  (*primary)->shoot(false);
  primary = --primary_weapons.end();
}

// Returns true if the player is currently holding the shield key.
// In that case we add ammo to a picked-up secondary without switching away.
static bool shield_held(const list<Weapon::Base*>& secondary_weapons,
                        list<Weapon::Base*>::const_iterator secondary) {
  if (secondary == secondary_weapons.end()) return false;
  return dynamic_cast<const Weapon::Shield*>(*secondary) && (*secondary)->is_shooting();
}

void Ship::add_mine_ammo(int amount) {
  for(auto it = secondary_weapons.begin(); it != secondary_weapons.end(); ++it) {
    if(dynamic_cast<Weapon::Mine*>(*it)) {
      (*it)->add_ammo(amount);
      if(!shield_held(secondary_weapons, secondary)) {
        (*secondary)->shoot(false);
        secondary = it;
      }
      return;
    }
  }
  Weapon::Mine *w = new Weapon::Mine(this);
  w->set_ammo(amount);
  secondary_weapons.push_back(w);
  if(!shield_held(secondary_weapons, secondary))
    secondary = --secondary_weapons.end();
}

void Ship::add_missile_ammo(int amount) {
  // Find the Missile weapon in secondary_weapons, add ammo and switch to it.
  for(auto it = secondary_weapons.begin(); it != secondary_weapons.end(); ++it) {
    if(dynamic_cast<Weapon::Missile*>(*it)) {
      (*it)->add_ammo(amount);
      if(!shield_held(secondary_weapons, secondary)) {
        (*secondary)->shoot(false);
        secondary = it;
      }
      return;
    }
  }
  Weapon::Missile *w = new Weapon::Missile(this);
  w->set_ammo(amount);
  if(missile_asteroids) w->set_asteroids(missile_asteroids);
  if(missile_ships_list) w->set_ship_targets(missile_ships_list);
  secondary_weapons.push_back(w);
  if(!shield_held(secondary_weapons, secondary))
    secondary = --secondary_weapons.end();
}

void Ship::add_shield_ammo(int amount) {
  for(auto it = secondary_weapons.begin(); it != secondary_weapons.end(); ++it) {
    if(dynamic_cast<Weapon::Shield*>(*it)) {
      (*it)->add_ammo(amount);
      // Only re-select the shield if it isn't already the active held weapon,
      // to avoid calling shoot(false) and briefly dropping the shield.
      if(secondary != it) {
        (*secondary)->shoot(false);
        secondary = it;
      }
      return;
    }
  }
  Weapon::Shield *w = new Weapon::Shield(this);
  w->set_ammo(amount);
  secondary_weapons.push_back(w);
  secondary = --secondary_weapons.end();
}

void Ship::set_missile_asteroids(std::list<Object*> *asteroids) {
  missile_asteroids = asteroids;
  for(auto it = secondary_weapons.begin(); it != secondary_weapons.end(); ++it) {
    Weapon::Missile *mw = dynamic_cast<Weapon::Missile*>(*it);
    if(mw) { mw->set_asteroids(asteroids); return; }
  }
}

void Ship::set_missile_ships(std::list<Object*> *ships) {
  missile_ships_list = ships;
  for(auto it = secondary_weapons.begin(); it != secondary_weapons.end(); ++it) {
    Weapon::Missile *mw = dynamic_cast<Weapon::Missile*>(*it);
    if(mw) { mw->set_ship_targets(ships); return; }
  }
}

void Ship::init(bool no_friction) {
  mass = 100.0;
  value = 1000000;
  lives = 4;
  width = height = radius = 15;
  radius_squared = radius * radius;
  respawn_time = time_until_respawn = 4000;
  max_temperature = 100.0;
  critical_temperature = max_temperature * 0.80;
  explode_temperature = max_temperature * 1.2;
  heat_rate = 0.000;
  retro_heat_rate = heat_rate * -reverse_force / thrust_force;
  cool_rate = retro_heat_rate * 0.9;
  boost_heat = 0.000;
  boost_force = 4.0;
  boosting = false;
  rotation_scale = 1.0f;
  thrust_analog  = 1.0f;
  reverse_analog = 1.0f;

  if(no_friction) {
    friction = 0;
    reverse_force = -0.01;
    thrust_force = 0.03;
    rotation_force = 0.5;
  } else {
    friction = 0.003;
    reverse_force = -0.1;
    thrust_force = 0.2;
    rotation_force = 0.5;
  }

  primary_weapons.push_front(new Weapon::Default(this));
  primary = primary_weapons.begin();

  secondary = secondary_weapons.end();

  facing = Point(0, 1);
  reset();
}

float Ship::temperature_ratio() {
  return temperature/max_temperature;
}

void Ship::respawn(const Grid &grid, bool was_killed) {
  if(alive || lives > 0) {
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
    time_left_invincible = 1500;
    reset(was_killed);
    safe_position(grid);
    invincible = true;
    detonate();
  }
}

void Ship::safe_position(const Grid &grid) {
  while(grid.collide(*this, 50.0f) != NULL) {
    position = WrappedPoint();
  }
}

void Ship::reset(bool was_killed) {
  velocity = Point(0, 0);
  thrusting = false;
  reversing = false;
  shoot(false);
  mines.clear();
  bullets.clear();
  missiles.clear();
  debris.clear();
  rotation_direction = NONE;
  still_rotating_left = false;
  still_rotating_right = false;
  temperature = 0.0;
  if(was_killed) {
    kills_this_life = 0;

    // Remove all upgraded primary weapons, keeping only the base PEW PEW at the front
    auto it = primary_weapons.begin();
    ++it;
    while(it != primary_weapons.end()) {
      delete *it;
      it = primary_weapons.erase(it);
    }
    primary = primary_weapons.begin();

    // Remove all secondary weapons (pickup-only)
    while(!secondary_weapons.empty()) {
      delete secondary_weapons.back();
      secondary_weapons.pop_back();
    }
    secondary = secondary_weapons.end();
  }
}

bool Ship::kill() {
  if(CompositeObject::kill()) {
    thrusting = false;
    reversing = false;
    rotation_direction = NONE;
    still_rotating_left = false;
    still_rotating_right = false;
    temperature = 0.0;
    time_until_respawn = respawn_time;
    if(boost_sound != NULL) {
      Mix_VolumeChunk(boost_sound, 0);
    }
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
  if(boost_sound != NULL) {
    if(on && !reversing) {
      Mix_VolumeChunk(boost_sound, MIX_MAX_VOLUME/8);
    }
    if(!on && !reversing) {
      if(still_rotating_left || still_rotating_right) {
        Mix_VolumeChunk(boost_sound, MIX_MAX_VOLUME/16);
      } else {
        Mix_VolumeChunk(boost_sound, 0);
      }
    }
  }
}

void Ship::reverse(bool on) {
  reversing = on;
  if(boost_sound != NULL) {
    if(on && !thrusting) {
      Mix_VolumeChunk(boost_sound, MIX_MAX_VOLUME/8);
    }
    if(!on && !thrusting) {
      if(still_rotating_left || still_rotating_right) {
        Mix_VolumeChunk(boost_sound, MIX_MAX_VOLUME/16);
      } else {
        Mix_VolumeChunk(boost_sound, 0);
      }
    }
  }
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
        // The hit object was invincible. If the ship is also invincible
        // (shielded), neither side died — but we still need to check whether
        // the ship is simultaneously touching a killable asteroid, because
        // grid.collide() stops at the first hit and would have skipped it.
        if(invincible) {
          Object *killable = grid.collide(*this, 0.0f, true);
          if(killable != NULL && killable->kill()) {
            detonate();
          }
        }
      }
      kill_stop();
    }
  }

  for(size_t i = 0; i < mines.size(); ) {
    object = grid.collide(mines[i], 50.0f);
    if(object != NULL && object->alive) {
      detonate(mines[i].position, mines[i].velocity, 50);
      mines[i] = std::move(mines.back());
      mines.pop_back();
    } else {
      ++i;
    }
  }

  for(size_t i = 0; i < bullets.size(); ) {
    object = grid.collide(bullets[i]);
    if(object != NULL) {
      if(object->kill()) {
        score += object->get_value() * multiplier();
        kills_this_life += 1;
        kills += 1;
      }
      explode(bullets[i].position, object->velocity);
      bullets[i] = std::move(bullets.back());
      bullets.pop_back();
    } else {
      ++i;
    }
  }

  for(size_t i = 0; i < missiles.size(); ) {
    object = grid.collide(missiles[i], 5.0f);
    if(object != NULL && object->alive) {
      object->kill();
      score += object->get_value() * multiplier();
      kills_this_life += 1;
      kills += 1;
      detonate(missiles[i].position, missiles[i].velocity, 50);
      if(missile_explode_sound != NULL) {
        Mix_PlayChannel(-1, missile_explode_sound, 0);
      }
      missiles[i] = std::move(missiles.back());
      missiles.pop_back();
    } else {
      ++i;
    }
  }
}

void Ship::collide(Ship *other) {
  for(size_t i = 0; i < bullets.size(); ) {
    if(other->is_alive() && bullets[i].collide(*other)) {
      other->kill();
      kills_this_life += 1;
      kills += 1;
      score += other->value * multiplier();
      bullets[i] = std::move(bullets.back());
      bullets.pop_back();
    } else {
      ++i;
    }
  }

  for(size_t i = 0; i < mines.size(); ) {
    if(is_alive() && other->is_alive() && mines[i].collide(*other, 50.0)) {
      detonate(mines[i].position, mines[i].velocity);
      mines[i] = std::move(mines.back());
      mines.pop_back();
    } else {
      ++i;
    }
  }

  for(size_t i = 0; i < missiles.size(); ) {
    if(is_alive() && other->is_alive() && missiles[i].collide(*other, 5.0)) {
      other->kill();
      kills_this_life += 1;
      kills += 1;
      score += other->value * multiplier();
      detonate(missiles[i].position, missiles[i].velocity, 50);
      if(missile_explode_sound != NULL) {
        Mix_PlayChannel(-1, missile_explode_sound, 0);
      }
      missiles[i] = std::move(missiles.back());
      missiles.pop_back();
    } else {
      ++i;
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
      previous_weapon();
    } else {
      (*primary)->shoot(on);
    }
  }
}

void Ship::mine(bool on) {
  if(secondary_weapons.empty()) return;
  if((*secondary)->empty() && on) {
    auto to_remove = secondary;
    auto next = to_remove;
    ++next;
    if(next == secondary_weapons.end())
      next = secondary_weapons.begin();
    delete *to_remove;
    secondary_weapons.erase(to_remove);
    secondary = secondary_weapons.empty() ? secondary_weapons.end() : next;
  } else {
    (*secondary)->shoot(on);
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
  play_rotating_sound(on);
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
  play_rotating_sound(on);
}

void Ship::play_rotating_sound(bool on) {
  if(boost_sound != NULL) {
    if(on && !thrusting && !reversing) {
      Mix_VolumeChunk(boost_sound, MIX_MAX_VOLUME/16);
    }
    if(!on) {
      if(thrusting || reversing) {
        Mix_VolumeChunk(boost_sound, MIX_MAX_VOLUME/8);
      } else {
        Mix_VolumeChunk(boost_sound, 0);
      }
    }
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

void Ship::step(float delta, const Grid &grid) {
  toggled = !toggled;
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
    for(auto it = secondary_weapons.begin(); it != secondary_weapons.end(); ++it) {
      if (!dynamic_cast<Weapon::Missile*>(*it))
        (*it)->step(delta);
    }

  } else if (lives > 0) {
    if(floor((time_until_respawn-1)/1000) != floor((time_until_respawn-delta-1)/1000)) {
      if(time_until_respawn > 1000) {
        if(tic_sound != NULL) {
          Mix_PlayChannel(-1, tic_sound, 0);
        }
      } else {
        if(tic_low_sound != NULL) {
          Mix_PlayChannel(-1, tic_low_sound, 0);
        }
      }
    }
    time_until_respawn -= delta;
    if(time_until_respawn < 0) {
      respawn(grid);
    }
  }

  facing.rotate(rotation_direction * rotation_force * rotation_scale / mass * delta);
  Point acceleration = Point(0,0);
  if(boosting) {
  	acceleration += ((facing * boost_force) / mass);
    temperature += boost_heat;
    explode();
    boosting = false;
  }
  if(thrusting) {
  	acceleration += ((facing * thrust_force * thrust_analog) / mass);
    temperature += heat_rate * delta;
	}
  if(reversing) {
  	acceleration += ((facing * reverse_force * reverse_analog) / mass);
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

  for(size_t i = 0; i < bullets.size(); ) {
    bullets[i].step(delta);
    if(!bullets[i].is_alive()) {
      bullets[i] = std::move(bullets.back());
      bullets.pop_back();
    } else {
      ++i;
    }
  }

  for(size_t i = 0; i < mines.size(); ) {
    mines[i].step(delta);
    if(!mines[i].is_alive()) {
      mines[i] = std::move(mines.back());
      mines.pop_back();
    } else {
      ++i;
    }
  }

  // Step missiles unconditionally so they keep flying regardless of weapon state.
  for(size_t i = 0; i < missiles.size(); i++) {
    missiles[i].step_missile(delta, missile_asteroids, missile_ships_list);
  }

  // Missile movement is handled in Weapon::Missile::step() above.
  // Here we only handle expiry detonation.
  for(size_t i = 0; i < missiles.size(); ) {
    if(!missiles[i].is_alive()) {
      detonate(missiles[i].position, missiles[i].velocity, 50);
      if(missile_explode_sound != NULL) {
        Mix_PlayChannel(-1, missile_explode_sound, 0);
      }
      missiles[i] = std::move(missiles.back());
      missiles.pop_back();
    } else {
      ++i;
    }
  }
}
