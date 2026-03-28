#include "glgame.h"
#include "highscore.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include "glship.h"
#include "glcar.h"
#include "glstarfield.h"
#include "wrapped_point.h"
#include "menu.h"
#include "state.h"
#include "asteroid.h"
#include "asteroid_drawer.h"
#include "object.h"
#include "grid.h"
#include "view/overlay.h"
#include "touch_controls.h"
#include <math.h>
#include <SDL.h>

#include "gl_compat.h"

#include <iostream>
#include <list>

const int GLGame::default_world_width = 2500;
const int GLGame::default_world_height = 2500;
const int GLGame::default_num_asteroids = 3;
const int GLGame::extra_num_asteroids = 5;
const float GLGame::extra_life_drop_chance = 0.003125f;
const float GLGame::weapon_pickup_drop_chance = 0.0125f;
const float GLGame::mine_pickup_drop_chance = 0.0125f;
const float GLGame::giga_mine_pickup_drop_chance = 0.005f;
const float GLGame::missile_pickup_drop_chance = 0.0125f;
const float GLGame::shield_pickup_drop_chance = 0.0125f;
const float GLGame::god_mode_pickup_drop_chance = 1.0f; // testing: always drop

GLGame::GLGame(SDL_GameController *controller) :
  State(),
  world(Point(default_world_width, default_world_height)),
  current_time(0),
  running(true),
  level_cleared(false),
  friendly_fire(true),
  debug_grid(false),
  score_saved(false),
  game_over_time(-1),
  grid(Grid(world, Point(Asteroid::max_radius*2,Asteroid::max_radius*2))) {
  time_between_steps = step_size;

  enemies = new std::list<GLShip*>;
  players = new std::list<GLShip*>;
  ship_objects = new std::list<Object*>;
  objects = new std::list<Asteroid*>;
  dead_objects = new std::list<Asteroid*>;
  pickups = new std::list<Pickup*>;
  black_holes = new std::list<BlackHole*>;

  WrappedPoint::set_boundaries(world);


  starfield = new GLStarfield(world);

  rearstars = glGenLists(1);
  frontstars = glGenLists(1);

  time_until_next_step = 0;
  num_frames = 0;

  generation = 0;
  Asteroid::num_killable = 0;
  add_asteroids();
  grid.update((std::list<Object *>*)objects);

  GLShip *object = new GLShip(grid, true);
  if(controller != NULL) {
    object->set_controller(controller);
  } else {
    object->set_keys('a','d','w',' ','s','x','q','e', 't', 128+GLUT_KEY_F1, 'c');
  }
  object->ship->set_missile_asteroids((std::list<Object*>*)objects);
  ship_objects->push_back(object->ship);
  object->ship->set_missile_ships(ship_objects);
  object->ship->set_black_holes(black_holes);
  players->push_back(object);

  station = NULL;//new GLStation(enemies, players);

  if(tic_sound == NULL) {
    tic_sound = Mix_LoadWAV("audio/tic.wav");
    if(tic_sound == NULL) {
      std::cout << "Unable to load tic.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(pickup_sound == NULL) {
    pickup_sound = Mix_LoadWAV("audio/pickup.wav");
    if(pickup_sound == NULL) {
      std::cout << "Unable to load pickup.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
}

GLGame::~GLGame() {
  //TODO: Make erase, use boost::ptr_list? something better
  // std::erase(std::remove_if(v.begin(),v.end(),true), v.end());
  while(!players->empty()) {
    delete players->back();
    players->pop_back();
  }
  delete players;
  while(!enemies->empty()) {
    delete enemies->back();
    enemies->pop_back();
  }
  delete enemies;
  delete ship_objects;
  while(!objects->empty()) {
    delete objects->back();
    objects->pop_back();
  }
  delete objects;
  while(!dead_objects->empty()) {
    delete dead_objects->back();
    dead_objects->pop_back();
  }
  delete dead_objects;
  while(!pickups->empty()) {
    delete pickups->back();
    pickups->pop_back();
  }
  delete pickups;
  while(!black_holes->empty()) {
    delete black_holes->back();
    black_holes->pop_back();
  }
  delete black_holes;
  delete starfield;
  if(station != NULL)
    delete station;

  if(tic_sound != NULL) {
    Mix_FreeChunk(tic_sound);
  }
  if(pickup_sound != NULL) {
    Mix_FreeChunk(pickup_sound);
  }
  glDeleteLists(rearstars, 1);
  glDeleteLists(frontstars, 1);
}

void GLGame::add_asteroids() {
  while(Asteroid::num_killable < (default_num_asteroids + generation * extra_num_asteroids)) {
    objects->push_back(new Asteroid(false));
    if(generation > 0) objects->push_front(new Asteroid(true));
  }
  int num_invisible = (generation >= 4) ? (generation - 4) / 5 + 1 : 0;
  for(int i = 0; i < num_invisible; i++) {
    objects->push_back(new Asteroid(false, true));
  }
  int num_reflective = generation / 2 + 1; // testing: spawn from level 1
  for(int i = 0; i < num_reflective; i++) {
    objects->push_front(new Asteroid(false, false, true));
  }
  int num_teleporting = (generation >= 3) ? (generation - 3) / 2 + 1 : 0;
  for(int i = 0; i < num_teleporting; i++) {
    objects->push_back(new Asteroid(false, false, false, true));
  }
  int num_quantum = (generation >= 5) ? (generation - 5) / 3 + 1 : 0;
  for(int i = 0; i < num_quantum; i++) {
    objects->push_back(new Asteroid(false, false, false, false, true));
  }
  int num_tough = generation / 2 + 1; // testing: spawn from level 1
  for(int i = 0; i < num_tough; i++) {
    objects->push_back(new Asteroid(false, false, false, false, false, true));
  }
}

void GLGame::toggle_pause() {
  running = !running;
  if (running) {
    Mix_Resume(-1);
  } else {
    Mix_Pause(-1);
  }
}

void GLGame::focus_lost() {
  if(running) {
    toggle_pause();
    auto_paused = true;
  }
  Mix_PauseMusic();
}

void GLGame::controller_added(SDL_GameController *ctrl) {
  SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(ctrl));
  // Skip if any player already has this controller
  for(auto* glship : *players) {
    if(glship->is_my_controller_id(id)) return;
  }
  // Assign to the first player who has no controller
  for(auto* glship : *players) {
    if(!glship->has_controller()) {
      glship->set_controller(ctrl);
      return;
    }
  }
}

bool GLGame::has_free_controller() const {
  int n = SDL_NumJoysticks();
  for(int i = 0; i < n; i++) {
    if(!SDL_IsGameController(i)) continue;
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(i);
    bool assigned = false;
    for(auto* glship : *players) {
      if(glship->is_my_controller_id(id)) { assigned = true; break; }
    }
    if(!assigned) return true;
  }
  return false;
}

void GLGame::controller_removed(SDL_JoystickID id) {
  for(auto* glship : *players) {
    if(glship->is_my_controller_id(id)) {
      glship->set_controller(NULL);
      if(running) toggle_pause();
      return;
    }
  }
}

void GLGame::add_player2(SDL_GameController *ctrl) {
  if(players->size() >= 2) return;
  Ship* p1 = players->front()->ship;
  if(!p1->is_alive() && !p1->lives) return;
  GLShip* object = new GLCar(grid, true);
  object->set_controller(ctrl);
  object->ship->set_missile_asteroids((std::list<Object*>*)objects);
  ship_objects->push_back(object->ship);
  for(auto *p : *players) p->ship->set_missile_ships(ship_objects);
  object->ship->set_missile_ships(ship_objects);
  object->ship->set_black_holes(black_holes);
  players->push_back(object);
}

void GLGame::focus_gained() {
  Mix_ResumeMusic();
  if(auto_paused) {
    toggle_pause();
    auto_paused = false;
  }
}

bool GLGame::cleared() const {
  return level_cleared;
}

void GLGame::tick(int delta) {
  current_time += delta;

  if (!running) {
    last_tick += delta;
    return;
  }

  time_until_next_step -= delta;

  num_frames++;

  if(Asteroid::num_killable == 0) {
    if(!level_cleared) {
      level_cleared = true;
      time_until_next_generation = 5000;
    } else if (time_until_next_generation > 0) {
      if(floor(time_until_next_generation/1000) != floor((time_until_next_generation-delta)/1000)) {
        if(tic_sound != NULL) {
          Mix_PlayChannel(-1, tic_sound, 0);
        }
      }
      time_until_next_generation -= delta;
    } else {
      generation++;
      if(generation == 20) {
        world += Point(3000, 3000);
      } else {
        world += Point(50, 50);
      }
      grid = Grid(world, Point(Asteroid::max_radius*2,Asteroid::max_radius*2));
      if(generation >= 20) {
        if(station != NULL)
          delete station;
        station = new GLStation(grid, enemies, players, (std::list<Object*>*)objects);
      }
      if(station != NULL) {
        station->reset();
      }
      delete starfield;
      starfield = new GLStarfield(world);
      WrappedPoint::set_boundaries(world);
      while(!objects->empty()) {
        delete objects->back();
        objects->pop_back();
      }
      while(!dead_objects->empty()) {
        delete dead_objects->back();
        dead_objects->pop_back();
      }
      Asteroid::num_killable = 0;
      add_asteroids();
      grid.update((std::list<Object *>*)objects);
      while(!pickups->empty()) {
        delete pickups->back();
        pickups->pop_back();
      }
      // Reposition the black hole at the new world centre.
      while(!black_holes->empty()) {
        delete black_holes->back();
        black_holes->pop_back();
      }
      if(generation > 10)
        black_holes->push_back(new BlackHole(WrappedPoint(world.x() / 2.0f, world.y() / 2.0f)));
      std::list<GLShip*>::iterator o;
      for(o = players->begin(); o != players->end(); o++) {
        (*o)->ship->respawn(grid, false);
      }
      level_cleared = false;
    }
  }

  std::list<GLShip*>::iterator o, o2;
  while(time_until_next_step <= 0) {
	/* STEP EVERYTHING */

    if(station != NULL) {
      station->step(step_size, grid);
    }

    // Step black holes (visual animation only).
    for(auto bhi = black_holes->begin(); bhi != black_holes->end(); bhi++) {
      (*bhi)->step(step_size);
    }

    std::list<Asteroid*>::iterator oi;
    for(oi = objects->begin(); oi != objects->end(); oi++) {
      (*oi)->step(step_size);
    }
    for(oi = dead_objects->begin(); oi != dead_objects->end(); oi++) {
      (*oi)->step(step_size);
    }

    // Apply black-hole gravity to asteroids (asteroids pass through, not swallowed).
    // Invincible asteroids are unaffected.
    for(auto bhi = black_holes->begin(); bhi != black_holes->end(); bhi++) {
      oi = objects->begin();
      while(oi != objects->end()) {
        if(!(*oi)->invincible)
          (*bhi)->apply_gravity(**oi, step_size);
        oi++;
      }
    }

    for(o = players->begin(); o != players->end(); o++) {
      (*o)->step(step_size, grid);
    }

    // Apply black-hole gravity to ships.
    for(auto bhi = black_holes->begin(); bhi != black_holes->end(); bhi++) {
      for(o = players->begin(); o != players->end(); o++) {
        if(!(*o)->ship->is_alive()) continue;
        if((*bhi)->apply_gravity(*(*o)->ship, step_size)) {
          (*o)->ship->kill();
        }
      }
      for(o = enemies->begin(); o != enemies->end(); o++) {
        if(!(*o)->ship->is_alive()) continue;
        if((*bhi)->apply_gravity(*(*o)->ship, step_size)) {
          (*o)->ship->kill();
        }
      }
    }

    // Apply black-hole gravity to bullets, missiles, and mines.
    for(auto bhi = black_holes->begin(); bhi != black_holes->end(); bhi++) {
      for(o = players->begin(); o != players->end(); o++) {
        for(auto &b : (*o)->ship->bullets)
          (*bhi)->apply_gravity(b, step_size);
        for(auto &m : (*o)->ship->missiles)
          (*bhi)->apply_gravity(m, step_size);
        for(auto &n : (*o)->ship->mines)
          (*bhi)->apply_gravity(n, step_size);
        for(auto &n : (*o)->ship->giga_mines)
          (*bhi)->apply_gravity(n, step_size);
      }
      for(o = enemies->begin(); o != enemies->end(); o++) {
        for(auto &b : (*o)->ship->bullets)
          (*bhi)->apply_gravity(b, step_size);
        for(auto &m : (*o)->ship->missiles)
          (*bhi)->apply_gravity(m, step_size);
        for(auto &n : (*o)->ship->mines)
          (*bhi)->apply_gravity(n, step_size);
        for(auto &n : (*o)->ship->giga_mines)
          (*bhi)->apply_gravity(n, step_size);
      }
    }

    for(o = enemies->begin(); o != enemies->end(); o++) {
      Ship* s = (*o)->ship;
      s->sound_volume_scale = is_visible_to_any_player(*s) ? 0.5f : 0.0f;
      (*o)->step(step_size, grid);
      // Re-apply boost volume after step since thrust() inside may have reset it
      if(s->boost_sound != NULL) {
        if(s->thrusting || s->reversing) {
          Mix_VolumeChunk(s->boost_sound, (int)(MIX_MAX_VOLUME/8 * s->sound_volume_scale));
        } else if(s->still_rotating_left || s->still_rotating_right) {
          Mix_VolumeChunk(s->boost_sound, (int)(MIX_MAX_VOLUME/16 * s->sound_volume_scale));
        } else {
          Mix_VolumeChunk(s->boost_sound, 0);
        }
      }
    }

    /* UPDATE COLLISION MAP */

    grid.update((std::list<Object *>*)objects);

  /* ELASTIC ASTEROID-ASTEROID COLLISIONS */
    // For each pair of live asteroids where at least one is elastic, apply
    // 2D elastic collision physics (mass ~ radius^2, conservation of momentum
    // and kinetic energy along the collision normal). Pairs are processed once
    // via inner iterator starting after outer to avoid double-application.
    // Reflective asteroids carry elastic=true so they participate automatically.
    {
      std::list<Asteroid*>::iterator ai, bi;
      for(ai = objects->begin(); ai != objects->end(); ++ai) {
        Asteroid *a = *ai;
        if(!a->alive) continue;
        bi = ai; ++bi;
        for(; bi != objects->end(); ++bi) {
          Asteroid *b = *bi;
          if(!b->alive) continue;
          if(!a->elastic && !b->elastic) continue;

          // Use world-wrap aware distance: get closest copy of A to B
          Point a_near = a->position.closest_to(b->position);
          float dx = a_near.x() - b->position.x();
          float dy = a_near.y() - b->position.y();
          float dist2 = dx * dx + dy * dy;
          float sum_r = a->radius + b->radius;
          if(dist2 >= sum_r * sum_r) continue; // no overlap

          float dist = sqrtf(dist2);
          if(dist < 1e-4f) continue; // degenerate overlap, skip

          // Collision normal pointing from B to A
          float nx = dx / dist;
          float ny = dy / dist;

          // Positional correction: always push apart to resolve overlap,
          // including the case where children spawn inside each other.
          float overlap = sum_r - dist;
          float push = overlap * 0.5f + 0.5f;
          a->position += Point(nx, ny) * push;
          a->position.wrap();
          b->position += Point(-nx, -ny) * push;
          b->position.wrap();

          // Velocity impulse: only when approaching (negative = approaching)
          float vrel_n = (a->velocity.x() - b->velocity.x()) * nx
                       + (a->velocity.y() - b->velocity.y()) * ny;
          if(vrel_n >= 0.0f) continue; // already separating, no impulse needed

          // Mass proportional to area (radius^2)
          float ma = a->radius * a->radius;
          float mb = b->radius * b->radius;
          float impulse = -2.0f * vrel_n * ma * mb / (ma + mb);

          a->velocity = a->velocity + Point(nx, ny) * (impulse / ma);
          b->velocity = b->velocity - Point(nx, ny) * (impulse / mb);
        }
      }
    }

  /* COLLIDE EVERYTHING */
    for(o = players->begin(); o != players->end(); o++) {
      (*o)->collide_grid(grid, step_size);
    }
    for(o = enemies->begin(); o != enemies->end(); o++) {
      (*o)->collide_grid(grid, step_size);
    }

    oi = objects->begin();
    while(oi != objects->end()) {
      if((*oi)->add_children(objects)) {
        if(!(*oi)->invincible) {
          float roll = rand() / float(RAND_MAX);
          if(roll < extra_life_drop_chance) {
            pickups->push_back(new ExtraLife((*oi)->position));
          } else if(roll < extra_life_drop_chance + weapon_pickup_drop_chance) {
            int weapon_index = rand() % 15;
            pickups->push_back(new WeaponPickup((*oi)->position, weapon_index));
          } else if(roll < extra_life_drop_chance + weapon_pickup_drop_chance + mine_pickup_drop_chance) {
            pickups->push_back(new MinePickup((*oi)->position));
          } else if(roll < extra_life_drop_chance + weapon_pickup_drop_chance + mine_pickup_drop_chance + giga_mine_pickup_drop_chance) {
            pickups->push_back(new GigaMinePickup((*oi)->position));
          } else if(roll < extra_life_drop_chance + weapon_pickup_drop_chance + mine_pickup_drop_chance + giga_mine_pickup_drop_chance + missile_pickup_drop_chance) {
            pickups->push_back(new MissilePickup((*oi)->position));
          } else if(roll < extra_life_drop_chance + weapon_pickup_drop_chance + mine_pickup_drop_chance + giga_mine_pickup_drop_chance + missile_pickup_drop_chance + shield_pickup_drop_chance) {
            pickups->push_back(new ShieldPickup((*oi)->position));
          } else if(roll < extra_life_drop_chance + weapon_pickup_drop_chance + mine_pickup_drop_chance + giga_mine_pickup_drop_chance + missile_pickup_drop_chance + shield_pickup_drop_chance + god_mode_pickup_drop_chance) {
            pickups->push_back(new GodModePickup((*oi)->position));
          }
        }
        // Move to dead_objects so the collision grid no longer iterates this
        // asteroid while its debris particles are still fading out.
        if(!(*oi)->is_removable()) {
          dead_objects->push_back(*oi);
          oi = objects->erase(oi);
          continue;
        }
      }
      if((*oi)->is_removable()) {
        delete *oi;
        oi = objects->erase(oi);
      } else {
        oi++;
      }
    }

    // Handle teleporting asteroids: relocate in the direction of the arrow.
    for(oi = objects->begin(); oi != objects->end(); ++oi) {
      Asteroid *ast = *oi;
      if(!ast->teleport_pending) continue;
      // Travel in teleport_angle direction, random distance from 200 to half world size.
      const float min_dist_from_ship = 400.0f;
      const float max_travel = fminf(world.x(), world.y()) * 0.5f;
      const float min_travel = 200.0f;
      WrappedPoint new_pos;
      for(int tries = 0; tries < 30; tries++) {
        float dist = min_travel + (rand() / (float)RAND_MAX) * (max_travel - min_travel);
        float ox = cosf(ast->teleport_angle) * dist;
        float oy = sinf(ast->teleport_angle) * dist;
        new_pos = WrappedPoint(ast->position.x() + ox, ast->position.y() + oy);
        new_pos.wrap();
        bool safe = true;
        for(auto po = players->begin(); po != players->end(); ++po) {
          if((*po)->ship->is_alive() &&
             new_pos.distance_to((*po)->ship->position) < min_dist_from_ship) {
            safe = false;
            break;
          }
        }
        if(safe) break;
      }
      ast->position = new_pos;
      ast->teleport_vulnerable = true;
      ast->vulnerable_time_left = 5000; // 5 seconds of vulnerability
      ast->teleport_pending = false;
      ast->teleport_angle = rand() / (float)RAND_MAX * 2.0f * (float)M_PI;
    }

    // Update quantum asteroid observation state: collapse when any player looks at
    // it (becomes killable, normal speed), enter superposition otherwise (invincible,
    // 3x speed so it can sneak up on players who look away).
    for(oi = objects->begin(); oi != objects->end(); ++oi) {
      Asteroid *ast = *oi;
      if(!ast->quantum) continue;
      bool now_observed = is_point_faced_by_any_player(ast->position);
      if(now_observed == ast->quantum_observed) continue;
      ast->quantum_observed = now_observed;
      float spd = ast->velocity.magnitude();
      if(spd > 1e-6f) {
        Point dir = ast->velocity * (1.0f / spd);
        if(now_observed) {
          // Collapse: slow to base speed, become killable
          ast->invincible = false;
          ast->velocity = dir * ast->quantum_base_speed;
        } else {
          // Superposition: speed up 6x, become invincible
          ast->invincible = true;
          ast->velocity = dir * ast->quantum_base_speed * 6.0f;
        }
      }
    }

    // Clean up dead asteroids whose debris has fully faded.
    oi = dead_objects->begin();
    while(oi != dead_objects->end()) {
      if((*oi)->is_removable()) {
        delete *oi;
        oi = dead_objects->erase(oi);
      } else {
        oi++;
      }
    }

    for(o = players->begin(); o != players->end(); o++) {
      if(friendly_fire) {
        for(o2 = o; o2 != players->end(); o2++) {
          if(*o != *o2) {
            GLShip::collide(*o, *o2);
          }
        }
      }
      for(o2 = enemies->begin(); o2 != enemies->end(); o2++) {
        GLShip::collide(*o, *o2);
      }
    }

    /* COLLIDE PLAYERS AND BULLETS WITH STATION */
    if (station != NULL && station->is_alive()) {
      for (o = players->begin(); o != players->end(); o++) {
        Ship* s = (*o)->ship;
        // Body collision: mirrors invincible-asteroid behaviour — impact
        // particles and velocity stop always; death particles only if killed.
        if (s->is_alive() && station->Object::collide(*s)) {
          station->hit();
          s->explode(s->position, station->velocity);
          s->kill_stop();
          if (!s->is_alive())
            s->detonate();
        }
        // Missile collision: consume missile, deal 1 damage, scatter debris
        // (debris bullets are then picked up by the bullet loop below)
        if (!station->is_alive()) break;
        for (size_t i = 0; i < s->missiles.size(); ) {
          if (station->Object::collide(s->missiles[i])) {
            station->hit();
            s->detonate(s->missiles[i].position, s->missiles[i].velocity, 25);
            if (s->missile_explode_sound != NULL)
              Mix_PlayChannel(-1, s->missile_explode_sound, 0);
            s->missiles[i] = std::move(s->missiles.back());
            s->missiles.pop_back();
            if (!station->is_alive()) break;
          } else {
            ++i;
          }
        }
        // Bullet collision: consume bullet, damage station
        if (!station->is_alive()) break;
        for (size_t i = 0; i < s->bullets.size(); ) {
          if (station->Object::collide(s->bullets[i])) {
            // Reflect debris off the station surface normal
            Point bpos = s->bullets[i].position;
            Point bvel = s->bullets[i].velocity;
            Point normal = Point(bpos.x() - station->position.x(),
                                 bpos.y() - station->position.y()).normalized();
            float dot = bvel.x() * normal.x() + bvel.y() * normal.y();
            Point reflected(bvel.x() - 2.0f * dot * normal.x(),
                            bvel.y() - 2.0f * dot * normal.y());
            s->explode(bpos, reflected.normalized() * station->velocity.magnitude());
            if (Asteroid::thud_sound != NULL)
              Mix_PlayChannel(-1, Asteroid::thud_sound, 0);
            station->hit();
            s->bullets[i] = std::move(s->bullets.back());
            s->bullets.pop_back();
            if (!station->is_alive()) break;
          } else {
            ++i;
          }
        }
      }
    }

    o = enemies->begin();
    while(o != enemies->end()) {
      if((*o)->is_removable()) {
        ship_objects->remove((*o)->ship);
        delete *o;
        o = enemies->erase(o);
      } else {
        o++;
      }
    }

    // Remove dead station once its debris has faded and all its ships are gone.
    if (station != NULL && station->is_removable() && enemies->empty()) {
      delete station;
      station = NULL;
    }

    /* COLLIDE PICKUPS WITH PLAYERS */
    for(o = players->begin(); o != players->end(); o++) {
      if(!(*o)->ship->is_alive()) continue;
      for(auto pi = pickups->begin(); pi != pickups->end(); pi++) {
        if(!(*pi)->collected && (*pi)->collide(*(*o)->ship)) {
          (*pi)->collected = true;
          (*pi)->apply((*o)->ship);
          if(pickup_sound != NULL)
            Mix_PlayChannel(-1, pickup_sound, 0);
        }
      }
    }

    /* STEP AND CLEAN UP PICKUPS */
    auto pi = pickups->begin();
    while(pi != pickups->end()) {
      if((*pi)->is_removable()) {
        delete *pi;
        pi = pickups->erase(pi);
      } else {
        (*pi)->step(step_size);
        pi++;
      }
    }

    time_until_next_step += time_between_steps;
  }
  /* Save high score automatically on game over */
  if (!score_saved && !players->empty()) {
    bool all_game_over = true;
    for (auto* glship : *players) {
      if (glship->ship->is_alive() || glship->ship->lives > 0) {
        all_game_over = false;
        break;
      }
    }
    if (all_game_over) {
      for (auto* glship : *players)
        save_high_score(glship->ship->score);
      score_saved = true;
      game_over_time = current_time;
#ifdef __EMSCRIPTEN__
      // Show the tap-to-continue overlay so any touch reaches _web_tap_start().
      EM_ASM(if (window.setMenuMode) window.setMenuMode(1););
#endif
    }
  }

  /* Display FPS */
  //std::cout << (num_frames*1000 / current_time) << std::endl;
}

void GLGame::draw_objects(float direction, bool minimap) const {
  if(debug_grid && !minimap) grid.draw_debug();

  for(auto bhi = black_holes->begin(); bhi != black_holes->end(); bhi++) {
    (*bhi)->draw(minimap);
  }

  AsteroidDrawer::draw_batch(objects, dead_objects, direction, minimap,
                             minimap ? world.x() : 0, minimap ? world.y() : 0);

  for(auto pi = pickups->begin(); pi != pickups->end(); pi++) {
    glPushMatrix();
    (*pi)->draw(direction);
    glPopMatrix();
  }

  std::list<GLShip*>::iterator o;
  for(o = players->begin(); o != players->end(); o++) {
    glPushMatrix();
    (*o)->draw(minimap);
    glPopMatrix();
  }
  for(o = enemies->begin(); o != enemies->end(); o++) {
    glPushMatrix();
    (*o)->draw(minimap);
    glPopMatrix();
  }

  if(station != NULL) station->draw(minimap);
}

void GLGame::draw(void) {
  glClear(GL_COLOR_BUFFER_BIT /*| GL_DEPTH_BUFFER_BIT*/);

  if(players->size() == 0) {
    draw_world();
  }
  else {
    if(players->size() > 0) {
      draw_world(players->front(), true);
    }
    if(players->size() > 1) {
      draw_world(players->back(), false);
    }
    //Draw map after - for partial translucency
    draw_map();
  }
}

void GLGame::setup_perspective(GLShip *glship) const {
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluPerspective(num_y_viewports() == 1 ? glship->view_angle() : glship->view_angle()*0.75, window.x()/num_x_viewports()/(window.y()/num_y_viewports()), 100.0f, 2000.0f);
  glMatrixMode(GL_MODELVIEW);
}

int GLGame::num_x_viewports() const {
  return (players->size() == 0) ? 1 : (window.x() > window.y()) ? players->size() : 1;
}

int GLGame::num_y_viewports() const {
  return (players->size() == 0) ? 1 : (window.x() > window.y()) ? 1 : players->size();
}

bool GLGame::is_visible_to_any_player(const Ship &ship) const {
  for(auto* glship : *players) {
    if(!glship->ship->is_alive()) continue;
    float fov_deg = glship->view_angle();
    float half_h = tanf(fov_deg * (float)M_PI / 360.0f) * 1000.0f;
    float aspect = window.x() / (float)(window.y() / num_y_viewports());
    float half_w = half_h * aspect;
    float cull_r2 = (half_w * half_w + half_h * half_h) * 1.1f;
    float dist = glship->ship->position.distance_to(ship.position);
    if(dist * dist <= cull_r2) return true;
  }
  return false;
}

bool GLGame::is_point_faced_by_any_player(Point p) const {
  for(auto* glship : *players) {
    Ship *s = glship->ship;
    if(!s->is_alive()) continue;
    // Get wrapped vector from ship to point
    Point ship_near = s->position.closest_to(p);
    float dx = p.x() - ship_near.x();
    float dy = p.y() - ship_near.y();
    // Project into the ship's facing frame:
    //   fwd  = component along facing direction (positive = in front)
    //   side = component along the right perpendicular of facing
    float fwd  = dx * s->facing.x() + dy * s->facing.y();
    float side = dx * s->facing.y() - dy * s->facing.x();
    // Viewport rectangle in world units (camera at z=1000, FOV-derived)
    float fov_deg = glship->view_angle();
    float half_h = tanf(fov_deg * (float)M_PI / 360.0f) * 1000.0f;
    float aspect = window.x() / (float)(window.y() / num_y_viewports());
    float half_w = half_h * aspect;
    // Reject if outside the on-screen rectangle
    if(fwd <= 0.0f || fwd > half_h || fabsf(side) > half_w) continue;
    // Facing cone: ±45° from ship heading
    float len = sqrtf(fwd * fwd + side * side);
    if(len < 1e-6f) return true;
    if(fwd / len > 0.7071f) return true;
  }
  return false;
}

void GLGame::setup_orthogonal() const {
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluOrtho2D(-window.x()/num_x_viewports(), window.x()/num_x_viewports(), -window.y()/num_y_viewports(), window.y()/num_y_viewports());
  glMatrixMode(GL_MODELVIEW);
}

void GLGame::setup_viewport(bool primary) const {
  if(players->size() > 1 && window.x() <= window.y()) {
    primary = !primary; //HACK: Fix this
  }
  glLoadIdentity();
  if(primary) {
    glViewport(0, 0, window.x()/num_x_viewports(), window.y()/num_y_viewports());
  } else {
    if(window.x() > window.y()) {
      glViewport(window.x()/num_x_viewports(), 0, window.x()/num_x_viewports(), window.y()/num_y_viewports());
    } else {
      glViewport(0, window.y()/num_y_viewports(), window.x()/num_x_viewports(), window.y()/num_y_viewports());
    }
  }
}

void GLGame::draw_world(GLShip *glship, bool primary) const {
  setup_perspective(glship);
  setup_viewport(primary);
  gluLookAt(0.0f, 0.0f, 1000.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f );
  draw_perspective(glship);
  setup_orthogonal();
  setup_viewport(primary);
  Overlay::draw(this, glship);
}

void GLGame::draw_perspective(GLShip *glship) const {
  /* Draw the world */
  Point position = (glship == NULL) ? Point(0,0) : glship->ship->position;
  float direction = (glship == NULL || !glship->rotate_view()) ? 0.0f : glship->camera_facing();

  // Starfields are static per-frame, so compile once and replay 9 times.
  glNewList(rearstars, GL_COMPILE);
    glTranslatef(-position.x(), -position.y(), 0.0f);
    starfield->draw_rear(position);
  glEndList();
  glNewList(frontstars, GL_COMPILE);
    glTranslatef(-position.x(), -position.y(), 0.0f);
    starfield->draw_front(position);
  glEndList();

  // Compute cull radius from actual viewport dimensions.
  // Camera is at z=1000; gluPerspective FOV is vertical.
  float fov_deg = glship ? glship->view_angle() : 85.0f;
  float half_h = tanf(fov_deg * (float)M_PI / 360.0f) * 1000.0f;
  float aspect = window.x() / (float)(window.y() / num_y_viewports());
  float half_w = half_h * aspect;
  float cull_r2 = (half_w * half_w + half_h * half_h) * 1.1f; // 10% margin for edge objects

  // Draw the world tessellated 3x3, culling tiles that are entirely off-screen.
  for(int x = -1; x <= 1; x++) {
    for(int y = -1; y <= 1; y++) {
      // Nearest distance from camera to tile rectangle (starfield tiles are
      // centered on world origin, not on the player).
      float smin_x = world.x()*x - position.x();
      float smax_x = smin_x + world.x();
      float smin_y = world.y()*y - position.y();
      float smax_y = smin_y + world.y();
      float snx = (smin_x > 0) ? smin_x : (smax_x < 0) ? -smax_x : 0;
      float sny = (smin_y > 0) ? smin_y : (smax_y < 0) ? -smax_y : 0;
      if (snx*snx + sny*sny > cull_r2) continue;

      glPushMatrix();
      glRotatef(direction, 0.0f, 0.0f, 1.0f);
      glTranslatef(world.x()*x, world.y()*y, 0.0f);
      glCallList(rearstars);
      glPopMatrix();
    }
  }
  // --- Invisible asteroid lensing: black asteroid polygon + shifted rear stars ---
  for(int x = -1; x <= 1; x++) {
    for(int y = -1; y <= 1; y++) {
      float smin_x = world.x()*x - position.x();
      float smax_x = smin_x + world.x();
      float smin_y = world.y()*y - position.y();
      float smax_y = smin_y + world.y();
      float snx = (smin_x > 0) ? smin_x : (smax_x < 0) ? -smax_x : 0;
      float sny = (smin_y > 0) ? smin_y : (smax_y < 0) ? -smax_y : 0;
      if (snx*snx + sny*sny > cull_r2) continue;

      for(list<Asteroid*>::const_iterator it = objects->begin(); it != objects->end(); ++it) {
        Asteroid const *a = *it;
        if (!a->invisible || !a->alive) continue;

        float ax = a->position.x();
        float ay = a->position.y();
        glPushMatrix();
        glRotatef(direction, 0.0f, 0.0f, 1.0f);
        glTranslatef(world.x()*x - position.x(), world.y()*y - position.y(), 0.0f);

        AsteroidDrawer::draw_invisible_mask(a, ax, ay);
        starfield->draw_stars_near(ax, ay, a->radius);

        glPopMatrix();
      }
    }
  }

  // Game objects: drawn directly each tile (no display list) so draw_batch
  // can emit all asteroids in two draw calls per tile instead of one per asteroid.
  for(int x = -1; x <= 1; x++) {
    for(int y = -1; y <= 1; y++) {
      // Nearest distance from camera to tile rect (objects span [0,world) per tile)
      float tmin_x = world.x()*x - position.x();
      float tmax_x = tmin_x + world.x();
      float tmin_y = world.y()*y - position.y();
      float tmax_y = tmin_y + world.y();
      float nx = (tmin_x > 0) ? tmin_x : (tmax_x < 0) ? -tmax_x : 0;
      float ny = (tmin_y > 0) ? tmin_y : (tmax_y < 0) ? -tmax_y : 0;
      if (nx*nx + ny*ny > cull_r2) continue;

      glPushMatrix();
      glRotatef(direction, 0.0f, 0.0f, 1.0f);
      glTranslatef(world.x()*x - position.x(), world.y()*y - position.y(), 0.0f);
      draw_objects(direction);
      glPopMatrix();
    }
  }
  for(int x = -1; x <= 1; x++) {
    for(int y = -1; y <= 1; y++) {
      float smin_x = world.x()*x - position.x();
      float smax_x = smin_x + world.x();
      float smin_y = world.y()*y - position.y();
      float smax_y = smin_y + world.y();
      float snx = (smin_x > 0) ? smin_x : (smax_x < 0) ? -smax_x : 0;
      float sny = (smin_y > 0) ? smin_y : (smax_y < 0) ? -smax_y : 0;
      if (snx*snx + sny*sny > cull_r2) continue;

      glPushMatrix();
      glRotatef(direction, 0.0f, 0.0f, 1.0f);
      glTranslatef(world.x()*x, world.y()*y, 0.0f);
      glCallList(frontstars);
      glPopMatrix();
    }
  }

  // --- Front star lensing (same void + shift, applied after front stars) ---
  for(int x = -1; x <= 1; x++) {
    for(int y = -1; y <= 1; y++) {
      float smin_x = world.x()*x - position.x();
      float smax_x = smin_x + world.x();
      float smin_y = world.y()*y - position.y();
      float smax_y = smin_y + world.y();
      float snx = (smin_x > 0) ? smin_x : (smax_x < 0) ? -smax_x : 0;
      float sny = (smin_y > 0) ? smin_y : (smax_y < 0) ? -smax_y : 0;
      if (snx*snx + sny*sny > cull_r2) continue;

      for(list<Asteroid*>::const_iterator it = objects->begin(); it != objects->end(); ++it) {
        Asteroid const *a = *it;
        if (!a->invisible || !a->alive) continue;

        float ax = a->position.x();
        float ay = a->position.y();
        glPushMatrix();
        glRotatef(direction, 0.0f, 0.0f, 1.0f);
        glTranslatef(world.x()*x - position.x(), world.y()*y - position.y(), 0.0f);

        // No black mask here — draw_batch already renders the invisible asteroid
        // fill behind visible asteroids. Drawing it again after game objects would
        // overdraw visible asteroids on top of invisible ones.
        starfield->draw_front_stars_near(ax, ay, a->radius);

        glPopMatrix();
      }
    }
  }

}

void GLGame::draw_map() const {
#if defined(__ANDROID__) || defined(__IOS__)
  return;
#endif
  float minimap_size = num_y_viewports() == 2 ? window.y()/6 : window.y()/4;

  if(players->size() > 1) {
    /* DRAW CENTER LINE */
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(-window.x(), window.x(), -window.y(), window.y());
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glViewport(0, 0, window.x(), window.y());
    glBegin(GL_LINES);
    glColor4f(1,1,1,0.5);
    if(window.x() < window.y()) {
      glVertex2f(-window.x(),0);
      glVertex2f(-minimap_size,0);
      glVertex2f(minimap_size,0);
      glVertex2f(window.x(),0);
    } else {
      glVertex2f(0,-window.y());
      glVertex2f(0,-minimap_size);
      glVertex2f(0, minimap_size);
      glVertex2f(0, window.y());
    }
    glEnd();
  }

  /* MINIMAP */
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluOrtho2D(0, world.x(), 0, world.y());
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  if (players->size() == 1) {
#if defined(__ANDROID__) || defined(__IOS__)
    // Shift the minimap right of the virtual joystick so they don't overlap.
    int map_x = (int)(g_touch_controls.joy_hint_cx + g_touch_controls.joy_radius + Overlay::CORNER_INSET);
#else
    int map_x = (int)Overlay::CORNER_INSET;
#endif
    glViewport(map_x, Overlay::CORNER_INSET, minimap_size, minimap_size);
  } else {
    glViewport(window.x()/2 - minimap_size/2, window.y()/2 - minimap_size/2, minimap_size, minimap_size);
  }

  /* BLACK BOX OVER MINIMAP */
  glColor4f(0.0f,0.0f,0.0f,0.8f);
  glBegin(GL_POLYGON);
    glVertex2i(  0, world.y());
    glVertex2i(  world.x(), world.y());
    glVertex2i(  world.x(), 0);
    glVertex2i(  0, 0);
  glEnd();

  // Single draw pass for minimap; wrapping tiles are negligible at minimap scale.
  glPushMatrix();
  draw_objects(0.0f, true);
  glPopMatrix();

  /* LINE AROUND MINIMAP */
  glColor3f(0.5f,0.5f,0.5f);
  glBegin(GL_LINE_LOOP);
    glVertex2i( 0, world.y());
    glVertex2i(  world.x(), world.y());
    glVertex2i(  world.x(),0);
    glVertex2i( 0,0);
  glEnd();
}

void GLGame::controller(SDL_Event event) {
  if(event.cbutton.type == SDL_CONTROLLERBUTTONDOWN) {
    if (event.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
      bool known_player = false;
      for(auto* glship : *players) {
        if(glship->wasMyController(event.cbutton.which)) {
          known_player = true;
          break;
        }
      }
      if(known_player) {
        toggle_pause();
      } else if(players->size() < 2) {
        SDL_GameController *ctrl = SDL_GameControllerFromInstanceID(event.cbutton.which);
        if(ctrl) add_player2(ctrl);
      }
    } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A ||
               event.cbutton.button == SDL_CONTROLLER_BUTTON_B ||
               event.cbutton.button == SDL_CONTROLLER_BUTTON_X ||
               event.cbutton.button == SDL_CONTROLLER_BUTTON_Y) {
      bool known_player = false;
      for(auto* glship : *players) {
        if(glship->wasMyController(event.cbutton.which)) {
          known_player = true;
          break;
        }
      }
      if(!known_player && players->size() < 2) {
        SDL_GameController *ctrl = SDL_GameControllerFromInstanceID(event.cbutton.which);
        if(ctrl) add_player2(ctrl);
      }
    } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE) {
      if(running) toggle_pause();
    } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
      for (auto* glship : *players)
        save_high_score(glship->ship->score);
      request_state_change(new Menu());
    }
  }

  if(!running)
    return;

  if(event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP) {
    std::list<GLShip*>::iterator object;
    for(object = players->begin(); object != players->end(); object++) {
      (*object)->controller_input(event);
    }
  }
  if(event.type == SDL_CONTROLLERAXISMOTION) {
    std::list<GLShip*>::iterator object;
    for(object = players->begin(); object != players->end(); object++) {
      (*object)->controller_axis_input(event);
    }
  }
}

void GLGame::touch_joystick(float nx, float ny) {
  if(!running || players->empty()) return;
  players->front()->touch_joystick_input(nx, ny);
}

void GLGame::keyboard (unsigned char key, int x, int y) {
  if (!running)
    return;

  std::list<GLShip*>::iterator object;
  for(object = players->begin(); object != players->end(); object++) {
    (*object)->input(key);
  }
}

void GLGame::keyboard_up (unsigned char key, int x, int y) {
  if (key == 'n') {
      level_cleared = true;
      time_until_next_generation = 0;
      while(!objects->empty()) {
        delete objects->back();
        objects->pop_back();
      }
      while(!dead_objects->empty()) {
        delete dead_objects->back();
        dead_objects->pop_back();
      }
      Asteroid::num_killable = 0;
  }

  if (key == 'g') {
    friendly_fire = !friendly_fire;
  }
  if (key == 'b') {
    debug_grid = !debug_grid;
  }
  if (key == '=' && time_between_steps > 1) time_between_steps--;
  if (key == '-') time_between_steps++;
  if (key == '0') time_between_steps = step_size;
  if (key == 'p') toggle_pause();
#if !defined(__ANDROID__) && !defined(__IOS__)
  if (key == 13 && players->size() < 2) {
    Ship* p1 = players->front()->ship;
    if(p1->is_alive() || p1->lives) {
      GLShip* object = new GLCar(grid, true);
      object->set_keys('j','l','i','/','k',',','u','o','y',128+GLUT_KEY_F8, '.');
      object->ship->set_missile_asteroids((std::list<Object*>*)objects);
      ship_objects->push_back(object->ship);
      for (auto *p : *players) p->ship->set_missile_ships(ship_objects);
      object->ship->set_missile_ships(ship_objects);
      players->push_back(object);
    }
  }
#endif
  // On all platforms: any non-ESC key goes to menu when all players are game over,
  // with a short delay so the last shoot input doesn't immediately skip the game over screen.
  if (key != 27) {
    bool all_game_over = !players->empty();
    for (auto* glship : *players) {
      if (glship->ship->is_alive() || glship->ship->lives > 0) {
        all_game_over = false;
        break;
      }
    }
    if (all_game_over) {
      if (game_over_time >= 0 && current_time - game_over_time < 3000)
        return;
      for (auto* glship : *players)
        save_high_score(glship->ship->score);
      request_state_change(new Menu());
      return;
    }
  }
  if (key == 27) {
    if (!running) {
      toggle_pause();
    } else {
      for (auto* glship : *players)
        save_high_score(glship->ship->score);
      request_state_change(new Menu());
    }
  }

  std::list<GLShip*>::iterator object;
  for(object = players->begin(); object != players->end(); object++) {
    (*object)->input(key, false);
  }
}
