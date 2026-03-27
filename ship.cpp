#include "ship.h"
#include "point.h"
#include "particle.h"
#include "asteroid.h"
#include "behaviour.h"
#include "weapon/base.h"
#include "weapon/default.h"
#include "weapon/mine.h"
#include "weapon/giga_mine.h"
#include "weapon/missile.h"
#include "weapon/shield.h"
#include "weapon/god_mode.h"
#include <math.h>
#include <climits>
#include <iostream>

using namespace std;

Ship::Ship(const Grid &grid, bool has_friction) :
    CompositeObject(),
    toggled(false) {
  alive = false;
  first_life = true;
  score = 0;
  kills = 0;
  bullet_trails.reserve(256);
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
    boost_channel = Mix_PlayChannel(-1, boost_sound, -1);
  } else {
    std::cout << "Unable to load boost.wav (" << Mix_GetError() << ")" << std::endl;
  }
  if(missile_explode_sound == NULL) {
    missile_explode_sound = Mix_LoadWAV("audio/missile_explode.wav");
    if(missile_explode_sound == NULL) {
      std::cout << "Unable to load missile_explode.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(shield_hum_sound == NULL) {
    shield_hum_sound = Mix_LoadWAV("audio/shield_hum.wav");
  }
  if(shield_hum_sound == NULL) {
    std::cout << "Unable to load shield_hum.wav (" << Mix_GetError() << ")" << std::endl;
  }
  if(explode_sound == NULL) {
    explode_sound = Mix_LoadWAV("audio/explode.wav");
    if(explode_sound == NULL) {
      std::cout << "Unable to load explode.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(giga_mine_explode_sound == NULL) {
    giga_mine_explode_sound = Mix_LoadWAV("audio/giga_mine_explode.wav");
    if(giga_mine_explode_sound == NULL) {
      std::cout << "Unable to load giga_mine_explode.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
}

void Ship::add_behaviour(Behaviour *b) {
  behaviours.push_back(b);
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
  if(boost_channel >= 0) {
    Mix_HaltChannel(boost_channel);
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
  if(shield_hum_channel >= 0) {
    Mix_HaltChannel(shield_hum_channel);
  }
  if(shield_hum_sound != NULL) {
    Mix_FreeChunk(shield_hum_sound);
  }
  if(explode_sound != NULL) {
    Mix_FreeChunk(explode_sound);
  }
  if(giga_mine_explode_sound != NULL) {
    Mix_FreeChunk(giga_mine_explode_sound);
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

void Ship::add_giga_mine_ammo(int amount) {
  for(auto it = secondary_weapons.begin(); it != secondary_weapons.end(); ++it) {
    if(dynamic_cast<Weapon::GigaMine*>(*it)) {
      (*it)->add_ammo(amount);
      if(!shield_held(secondary_weapons, secondary)) {
        (*secondary)->shoot(false);
        secondary = it;
      }
      return;
    }
  }
  Weapon::GigaMine *w = new Weapon::GigaMine(this);
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

void Ship::add_god_mode(int duration_ms) {
  for(auto it = primary_weapons.begin(); it != primary_weapons.end(); ++it) {
    if(dynamic_cast<Weapon::GodMode*>(*it)) {
      (*it)->set_ammo(duration_ms);
      invincible = true;
      time_left_invincible = INT_MAX;
      return;
    }
  }
  primary_weapons.push_back(new Weapon::GodMode(this, duration_ms));
  (*primary)->shoot(false);
  primary = --primary_weapons.end();
}

int Ship::god_mode_time_remaining() const {
  for(auto it = primary_weapons.begin(); it != primary_weapons.end(); ++it) {
    Weapon::GodMode *gm = dynamic_cast<Weapon::GodMode*>(*it);
    if(gm) return gm->time_remaining();
  }
  return 0;
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

void Ship::set_black_holes(const std::list<BlackHole*> *bhs) {
  black_holes = bhs;
}

void Ship::init(bool no_friction) {
  mass = 100.0;
  value = 200;
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
    bool try_current_position = first_life;
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
    safe_position(grid, try_current_position);
    invincible = true;
    set_shield_hum(true);
    detonate();
  }
}

void Ship::safe_position(const Grid &grid, bool try_current) {
  auto in_black_hole_pull = [this]() {
    if(!black_holes) return false;
    for(const BlackHole *bh : *black_holes) {
      Point diff = bh->position.closest_to(position) - position;
      if(diff.magnitude_squared() < BlackHole::influence_radius * BlackHole::influence_radius)
        return true;
    }
    return false;
  };
  if(try_current && grid.collide(*this, 50.0f) == NULL && !in_black_hole_pull())
    return;
  do {
    position = WrappedPoint();
  } while(grid.collide(*this, 50.0f) != NULL || in_black_hole_pull());
}

void Ship::reset(bool was_killed) {
  velocity = Point(0, 0);
  thrusting = false;
  reversing = false;
  shoot(false);
  mines.clear();
  giga_mines.clear();
  bullets.clear();
  missiles.clear();
  shockwaves.clear();
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
    set_shield_hum(false);
    if(explode_sound != NULL && sound_volume_scale > 0.0f) {
      Mix_PlayChannel(-1, explode_sound, 0);
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
  // Weapon-to-ship collisions
  //TODO: DRYness
  first->collide(second);
  second->collide(first);

  // Body-to-body collision: if the two ships physically overlap, kill the
  // non-invincible one(s).  If both are invincible nothing happens.
  if(first->is_alive() && second->is_alive() && first->Object::collide(*second)) {
    if(!first->invincible && !second->invincible) {
      first->kill_stop();
      first->detonate();
      second->kill_stop();
      second->detonate();
    } else if(!first->invincible) {
      first->kill_stop();
      first->detonate();
    } else if(!second->invincible) {
      second->kill_stop();
      second->detonate();
    }
  }
}

int Ship::multiplier() const {
  return kills_this_life / 10 + 1;
}

void Ship::collide_grid(Grid &grid, int delta) {
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

  for(size_t i = 0; i < giga_mines.size(); ) {
    object = grid.collide(giga_mines[i], 50.0f);
    if(object != NULL && object->alive) {
      giga_detonate(giga_mines[i].position);
      giga_mines[i] = std::move(giga_mines.back());
      giga_mines.pop_back();
    } else {
      ++i;
    }
  }

  // Process active shockwaves: kill everything in the expanding ring
  for(auto &sw : shockwaves) {
    if(!missile_asteroids) continue;
    for(auto it = missile_asteroids->begin(); it != missile_asteroids->end(); ++it) {
      Object *obj = *it;
      if(!obj->alive || obj->invincible) continue;
      float dist = (obj->position - sw.position).magnitude();
      // Kill objects swept by the ring (between prev_radius and current_radius)
      if(dist <= sw.radius && dist > sw.prev_radius - obj->radius) {
        if(obj->kill()) {
          score += obj->get_value() * multiplier();
          kills_this_life += 1;
          kills += 1;
        }
      }
    }
  }

  vector<Object *> sweep_candidates;
  for(size_t i = 0; i < bullets.size(); ) {
    // Swept collision via segment-polygon intersection: test the bullet's
    // full path this frame against asteroid polygon edges so fast bullets
    // cannot tunnel through.
    Point seg_a(bullets[i].position.x() - bullets[i].velocity.x() * delta,
                bullets[i].position.y() - bullets[i].velocity.y() * delta);
    Point seg_b(bullets[i].position.x(), bullets[i].position.y());
    sweep_candidates.clear();
    grid.query_segment(seg_a, seg_b, sweep_candidates);
    object = NULL;
    float best_t = 2.0f;
    for (Object *cand : sweep_candidates) {
      Asteroid *ast = dynamic_cast<Asteroid*>(cand);
      if (!ast) continue;
      // When the bullet's path crosses a world edge, seg_a can lie outside
      // [x_min, x_max] while the asteroid sits near the opposite edge.
      // Translate both endpoints into the same world-copy as the asteroid so
      // the segment/polygon tests produce correct results.
      Point ast_near = ast->position.closest_to(seg_a);
      float ox = ast_near.x() - ast->position.x();
      float oy = ast_near.y() - ast->position.y();
      Point local_a(seg_a.x() - ox, seg_a.y() - oy);
      Point local_b(seg_b.x() - ox, seg_b.y() - oy);
      float t;
      bool edge_hit = ast->segment_hit(local_a, local_b, t);
      if (edge_hit && t < best_t) {
        best_t = t;
        object = cand;
      } else if (!edge_hit && ast->contains(local_b) && 1.0f < best_t) {
        // Bullet is inside the asteroid without crossing its edge this frame
        // (e.g. fired point-blank into a large asteroid, or asteroid moved
        // over the bullet). Treat as a hit at the bullet's current position.
        best_t = 1.0f;
        object = cand;
      }
    }
    if (object != NULL) {
      bullets[i].position = WrappedPoint(seg_a.x() + (seg_b.x() - seg_a.x()) * best_t,
                                         seg_a.y() + (seg_b.y() - seg_a.y()) * best_t);
    }
    if(object != NULL) {
      Asteroid *ast = dynamic_cast<Asteroid*>(object);
      if(ast && ast->reflective && !bullets[i].has_trail) {
        // Back-trace along the bullet's velocity to find where it crossed the surface.
        // The bullet is guaranteed to be inside the polygon here; stepping backward
        // by 1px increments finds the entry point in ~10 steps for typical bullet speeds.
        Point vel_norm = bullets[i].velocity.normalized();
        float max_trace = ast->effective_radius() * 2.0f + 4.0f;
        WrappedPoint entry = bullets[i].position;
        for (float d = 1.0f; d <= max_trace; d += 1.0f) {
          WrappedPoint test(bullets[i].position.x() - vel_norm.x() * d,
                            bullets[i].position.y() - vel_norm.y() * d);
          if (!ast->contains(test)) {
            entry = test;
            break;
          }
        }
        // Use the edge normal most facing the bullet so corner hits reflect
        // away from the struck face, not along its bisector.
        Point rel_vel = bullets[i].velocity - object->velocity;
        Point normal = ast->surface_normal(entry, rel_vel);
        // Reflect in the asteroid's reference frame so a chasing asteroid
        // doesn't immediately catch the bullet again.
        float dot = normal.x() * rel_vel.x() + normal.y() * rel_vel.y();
        bullets[i].velocity = object->velocity + (rel_vel - normal * (2.0f * dot));
        // Push out by a full step's worth of relative travel so glancing hits
        // (tiny outward component) still clear the surface next frame.
        float push = rel_vel.magnitude() * 16.0f + 2.0f;
        bullets[i].position = WrappedPoint(entry.x() + normal.x() * push,
                                           entry.y() + normal.y() * push);
        object->kill(); // plays thud sound
        bullets[i].world_bullet = true;
        ++i;
      } else {
        bool was_invincible = object->invincible;
        if(bullets[i].has_trail) object->invincible = false;
        if(object->kill()) {
          object->invincible = was_invincible;
          if(was_invincible) Asteroid::num_killable++;
          score += object->get_value() * multiplier();
          kills_this_life += 1;
          kills += 1;
        }
        explode(bullets[i].position, object->velocity);
        bullets[i] = std::move(bullets.back());
        bullets.pop_back();
      }
    } else {
      ++i;
    }
  }

  // World bullets (ricocheted off reflective asteroids) can kill their owner.
  for(size_t i = 0; i < bullets.size(); ) {
    if(bullets[i].world_bullet && is_alive() && bullets[i].collide(*this)) {
      kill();
      explode(bullets[i].position, bullets[i].velocity);
      bullets[i] = std::move(bullets.back());
      bullets.pop_back();
    } else {
      ++i;
    }
  }

  for(size_t i = 0; i < missiles.size(); ) {
    object = grid.collide(missiles[i], 5.0f);
    if(object != NULL && object->alive) {
      if(object->kill()) {
        score += object->get_value() * multiplier();
        kills_this_life += 1;
        kills += 1;
      }
      detonate(missiles[i].position, missiles[i].velocity, 25);
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

  for(size_t i = 0; i < giga_mines.size(); ) {
    if(is_alive() && other->is_alive() && giga_mines[i].collide(*other, 50.0)) {
      giga_detonate(giga_mines[i].position);
      giga_mines[i] = std::move(giga_mines.back());
      giga_mines.pop_back();
    } else {
      ++i;
    }
  }

  // Shockwave kills other ships
  for(auto &sw : shockwaves) {
    if(!other->is_alive()) break;
    float dist = (other->position - sw.position).magnitude();
    if(dist <= sw.radius && dist + other->radius > sw.prev_radius) {
      other->kill_stop();
    }
  }

  for(size_t i = 0; i < missiles.size(); ) {
    if(is_alive() && other->is_alive() && missiles[i].collide(*other, 5.0)) {
      other->kill();
      kills_this_life += 1;
      kills += 1;
      score += other->value * multiplier();
      detonate(missiles[i].position, missiles[i].velocity, 25);
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

void Ship::giga_detonate(Point const position) {
  if(giga_mine_explode_sound != NULL) {
    Mix_PlayChannel(-1, giga_mine_explode_sound, 0);
  }
  // Launch expanding shockwave ring: radius grows to max_radius over ~700ms
  float max_r = (float)Asteroid::max_radius;
  float duration = 700.0f;
  float speed = max_r / duration;
  shockwaves.push_back(Shockwave(position, max_r, speed, duration));
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

void Ship::fire_secondary(bool on) {
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

void Ship::set_shield_hum(bool on) {
  if(shield_hum_sound == NULL || sound_volume_scale < 1.0f) return;
  if(on) {
    if(shield_hum_channel >= 0) return; // already playing, don't leak a new channel
    shield_hum_channel = Mix_PlayChannel(-1, shield_hum_sound, -1);
  } else if(shield_hum_channel >= 0) {
    Mix_HaltChannel(shield_hum_channel);
    shield_hum_channel = -1;
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

void Ship::mark_last_bullet_trail() {
  if(!bullets.empty())
    bullets.back().has_trail = true;
}

void Ship::fire_bullet_from_gun() {
  bullets.push_back(Particle(gun(), facing * 0.615f + velocity * 0.99f, 2000.0f));
  mark_last_bullet_trail();
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
        set_shield_hum(false);
      }
    }

    for(auto it = primary_weapons.begin(); it != primary_weapons.end(); ) {
      (*it)->step(delta);
      Weapon::GodMode *gm = dynamic_cast<Weapon::GodMode*>(*it);
      if(gm && gm->empty()) {
        auto next = it; ++next;
        if(next == primary_weapons.end()) next = primary_weapons.begin();
        if(it == primary) primary = next;
        delete *it;
        it = primary_weapons.erase(it);
      } else {
        ++it;
      }
    }
    for(auto it = secondary_weapons.begin(); it != secondary_weapons.end(); ++it) {
      if (!dynamic_cast<Weapon::Missile*>(*it))
        (*it)->step(delta);
    }

  } else if (lives > 0) {
    if(sound_volume_scale >= 1.0f) {
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
      if(bullets[i].has_trail) {
        bullets[i].trail_timer -= delta;
        if(bullets[i].trail_timer <= 0) {
          bullets[i].trail_timer = 50;
          Point spread((rand()%100-50)*0.0002f, (rand()%100-50)*0.0002f);
          bullet_trails.push_back(Particle(bullets[i].position, spread, 200.0f));
        }
      }
      ++i;
    }
  }

  for(size_t i = 0; i < bullet_trails.size(); ) {
    bullet_trails[i].step(delta);
    if(!bullet_trails[i].is_alive()) {
      bullet_trails[i] = std::move(bullet_trails.back());
      bullet_trails.pop_back();
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

  for(size_t i = 0; i < giga_mines.size(); ) {
    giga_mines[i].step(delta);
    if(!giga_mines[i].is_alive()) {
      giga_mines[i] = std::move(giga_mines.back());
      giga_mines.pop_back();
    } else {
      ++i;
    }
  }

  for(size_t i = 0; i < shockwaves.size(); ) {
    shockwaves[i].step(delta);
    if(!shockwaves[i].alive()) {
      shockwaves[i] = std::move(shockwaves.back());
      shockwaves.pop_back();
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
      detonate(missiles[i].position, missiles[i].velocity, 25);
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
