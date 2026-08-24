#include "glstation.h"
#include <math.h>
#include <cstdlib>

#include "gl_compat.h"
#include "mesh.h"
#include "mat4.h"

#include "ship.h"
#include "glenemy.h"
#include <list>
#include <iostream>
#include "savegame.h"
#include "grid.h"
#include "follower.h"
// #include "follower.h"

using namespace std;

GLStation::GLStation(const Grid &grid, list<GLShip*>* objects, list<GLShip*>* targets, list<Object*>* asteroids, float aim_lead, int generation, list<Object*>* missile_ships) : Ship(grid, false), objects(objects), targets(targets), asteroids(asteroids), aim_lead(aim_lead), generation(generation), missile_ships(missile_ships) {
  position = Point(0,0);
  radius = 200.0;
  time_until_respawn = 0;
  velocity = Point(0.1f, 0.0f);
  radius_squared = radius * radius;
  max_ships_per_wave = 50;
  extra_ships_per_wave = 1;
  ships_left_to_deploy = ships_this_wave = targets->size();
  time_until_next_ship = 5000;
  time_between_ships = 500;
  deploying = true;
  redeploying = false;
  wave = difficulty = 0;
  lives = 1;
  health = 100;
  alive = true;

  // behaviours.push_back(new Roamer(this));

  // The station never thrusts: release the looping engine-hum channel the
  // Ship constructor started (same as GLMiniStation), or it holds one of
  // the mixer's channels for its whole life.
  mute_engine();

  outer_rotation_speed = 0.01;
  inner_rotation_speed = -0.0025;
  inner_rotation = outer_rotation = 0;

  float r = radius, r2 = radius * 0.9f;
  float segment_size = 360.0f / NUM_SEGMENTS;

  {
    MeshBuilder mb;
    // Black filled disc (centre vertex first for proper fan)
    mb.begin(GL_TRIANGLE_FAN);
    mb.color(0.0f, 0.0f, 0.0f);
    mb.vertex(0.0f, 0.0f);
    for (float i = 0.0f; i <= 360.0f; i += segment_size) {
      float d = i * (float)M_PI / 180.0f;
      mb.vertex(r*cosf(d), r*sinf(d));
    }
    mb.end();
    // Outer ring circle
    mb.begin(GL_LINE_LOOP);
    mb.color(1.0f, 1.0f, 1.0f);
    for (float i = 0.0f; i < 360.0f; i += segment_size) {
      float d = i * (float)M_PI / 180.0f;
      mb.vertex(r*cosf(d), r*sinf(d));
    }
    mb.end();
    // Inner ring circle
    mb.begin(GL_LINE_LOOP);
    mb.color(1.0f, 1.0f, 1.0f);
    for (float i = 0.0f; i < 360.0f; i += segment_size) {
      float d = i * (float)M_PI / 180.0f;
      mb.vertex(r2*cosf(d), r2*sinf(d));
    }
    mb.end();
    // Radial divider spokes
    mb.begin(GL_LINES);
    mb.color(1.0f, 1.0f, 1.0f);
    for (float i = 0.0f; i < 360.0f; i += segment_size) {
      float d = i * (float)M_PI / 180.0f;
      mb.vertex(r *cosf(d), r *sinf(d));
      mb.vertex(r2*cosf(d), r2*sinf(d));
    }
    mb.end();
    body_mesh.upload(mb);
  }

  {
    MeshBuilder mb;
    mb.begin(GL_TRIANGLE_FAN);
    mb.color(1.0f, 1.0f, 1.0f);
    float mseg = 360.0f / 8;
    for (float i = 0.0f; i < 360.0f; i += mseg) {
      float d = i * (float)M_PI / 180.0f;
      mb.vertex(r*cosf(d), r*sinf(d));
    }
    mb.end();
    map_body_mesh.upload(mb);
  }

  if (armoured()) {
    // Gen-19+ shield arc: a bright band just outside the ring in the
    // armoured-asteroid indicator cyan, spanning arc_coverage_deg centred
    // on local angle 0 — draw_at(outer_rotation) then puts the arc centre
    // at world bearing outer_rotation, the exact span deflects() tests.
    // Radial end caps mark the gap edges so the weak window reads at a
    // glance.
    MeshBuilder mb;
    const float half = arc_coverage_deg() / 2.0f;
    const float arc_r = radius * 1.12f;
    mb.begin(GL_LINES);
    mb.color(0.3f, 0.9f, 1.0f, 0.9f);
    const int segs = 48;
    for (int i = 0; i < segs; i++) {
      float a0 = (-half + arc_coverage_deg() * i / segs) * (float)M_PI / 180.0f;
      float a1 = (-half + arc_coverage_deg() * (i + 1) / segs) * (float)M_PI / 180.0f;
      mb.vertex(arc_r * cosf(a0), arc_r * sinf(a0));
      mb.vertex(arc_r * cosf(a1), arc_r * sinf(a1));
    }
    for (int e = -1; e <= 1; e += 2) {
      float a = e * half * (float)M_PI / 180.0f;
      mb.vertex(radius * 1.03f * cosf(a), radius * 1.03f * sinf(a));
      mb.vertex(radius * 1.19f * cosf(a), radius * 1.19f * sinf(a));
    }
    mb.end();
    arc_mesh.upload(mb);
  }
}

const int GLStation::REWARD = 2500;

bool GLStation::armoured() const {
  return generation >= 19;
}

float GLStation::arc_coverage_deg() const {
  // 240 at gen 19, +8 per generation after, capped so the gap the plain
  // gun needs never shrinks below 60 degrees.
  float cov = 240.0f + 8.0f * (generation - 19);
  return cov < 300.0f ? cov : 300.0f;
}

float GLStation::arc_center_deg() const {
  return outer_rotation;
}

int GLStation::bounty() const {
  return armoured() ? REWARD : 0;
}

bool GLStation::deflects(const WrappedPoint &impact) const {
  if (!armoured() || !alive) return false;
  // Bearing of the impact point from the station centre (wrap-aware:
  // measure against the station copy nearest the impact), against the
  // arc span centred on outer_rotation — the drawn arc's own convention.
  Point c = position.closest_to(impact);
  float ix = impact.x() - c.x(), iy = impact.y() - c.y();
  if (ix * ix + iy * iy < 1e-8f) return true;  // dead centre: call it shielded
  float bearing = atan2f(iy, ix) * 180.0f / (float)M_PI;
  float diff = fmodf(bearing - arc_center_deg(), 360.0f);
  if (diff > 180.0f) diff -= 360.0f;
  if (diff < -180.0f) diff += 360.0f;
  return fabsf(diff) <= arc_coverage_deg() / 2.0f;
}

GLStation::~GLStation() {
  // delete targets;
  // delete objects;
}

void GLStation::reset(bool was_killed) {
  while(!objects->empty()) {
    if(objects->back()->ship->is_alive()) {
      ships_left_to_deploy++;
    }
    if(missile_ships) missile_ships->remove(objects->back()->ship);
    delete objects->back();
    objects->pop_back();
  }
  time_until_next_ship = 5000;
  deploying = redeploying = true;
}

void GLStation::hit() {
  if (!alive) return;
  if (--health <= 0)
    destroy();
}

void GLStation::destroy() {
  alive = false;
  lives = 0;
  // Large radial burst: 300 particles, all guaranteed to expand beyond radius.
  // Speed 0.15–0.60 units/ms × TTL 1500–2500ms → min reach 225 units > 200.
  int count = 300;
  debris.reserve(debris.size() + count);
  for (int i = 0; i < count; i++) {
    float angle = (float)(rand() % 100000) / 100000.0f * 2.0f * (float)M_PI;
    float dist  = (float)(rand() % (int)radius);
    Point start(position.x() + dist * cosf(angle),
                position.y() + dist * sinf(angle));
    float speed = 0.15f + (float)(rand() % 100) / 222.0f;
    Point vel(speed * cosf(angle), speed * sinf(angle));
    debris.push_back(Particle(start, vel, 1500.0f + rand() % 1000));
  }
}

int GLStation::level() const {
  return wave;
}

int GLStation::wave_interceptors() const {
  if (generation < 15) return 0;
  int allowed = 1 + (generation - 15);
  int cap = (ships_this_wave + 1) / 2;
  return allowed < cap ? allowed : cap;
}

int GLStation::wave_rammers() const {
  // The deliberate rammer joins from generation 20, its allotment growing
  // like the other specialists'. It takes the slots the interceptor
  // vanguard and the reserved standard leave, AHEAD of the bombers (see
  // the header note: the newest specialist must actually debut).
  if (generation < 20) return 0;
  int allowed = 1 + (generation - 20);
  int reserve = ships_this_wave >= 3 ? 1 : 0;
  int cap = ships_this_wave - wave_interceptors() - reserve;
  if (cap < 0) cap = 0;
  return allowed < cap ? allowed : cap;
}

int GLStation::wave_bombers() const {
  // The rear-line artillery joins from generation 17, its allotment
  // growing like the interceptors'. The cap stops it eating into the
  // interceptor vanguard (and, from gen 20, the rammer charge), and any
  // wave of 3+ ships RESERVES one standard slot — by gen 19 the two
  // specialist allotments (interceptors 5, bombers 3) otherwise swallowed
  // every wave up to size 7 and the green line vanished until wave 8
  // (field, 2026-08-23). Waves of 1-2 skip the reservation so the gen-17
  // bomber still debuts by wave 2, the promise its intro just made.
  if (generation < 17) return 0;
  int allowed = 1 + (generation - 17);
  int reserve = ships_this_wave >= 3 ? 1 : 0;
  int cap = ships_this_wave - wave_interceptors() - wave_rammers() - reserve;
  if (cap < 0) cap = 0;
  return allowed < cap ? allowed : cap;
}

void GLStation::draw(bool minimap) const {
  float px = position.x(), py = position.y();

  if(minimap && alive) {
    map_body_mesh.draw_at(px, py, 0.0f);
  } else if(alive) {
    glLineWidth(2.5f);
    body_mesh.draw_at(px, py, outer_rotation);
    // Inner ring: translate + rotate + scale(0.8)
    float inner_model[16]; mat4_identity(inner_model);
    mat4_translate(inner_model, inner_model, px, py, 0.0f);
    mat4_rotate_z(inner_model, inner_model, inner_rotation);
    mat4_scale(inner_model, inner_model, 0.8f, 0.8f, 1.0f);
    body_mesh.draw_with_model(inner_model);
    if (armoured()) {
      // The shield arc rides the outer ring's rotation — the same value
      // deflects() tests, so the picture and the physics can't disagree.
      glLineWidth(4.0f);
      arc_mesh.draw_at(px, py, outer_rotation);
      glLineWidth(2.5f);
    }
  }

  if (!minimap && !debris.empty()) {
    // Hazard-style ember diamonds, not GL_POINTS: the field GPU that
    // silently dropped the point-debris draws (seeker burst, player
    // death, exhaust trail — all under late-game load) would take the
    // station's 300-ember payoff next. Same geometry and palette as
    // Hazard::draw's burst.
    static MeshBuilder mb;
    static Mesh mesh;
    mb.clear();
    mb.begin(GL_TRIANGLES);
    for (const auto& d : debris) {
      float al = d.aliveness();
      float a = al > 0.6f ? 1.0f : al / 0.6f;
      float x = d.position.x(), y = d.position.y();
      float vx = d.velocity.x(), vy = d.velocity.y();
      float vm = sqrtf(vx * vx + vy * vy);
      float dx = vm > 1e-5f ? vx / vm : 1.0f;
      float dy = vm > 1e-5f ? vy / vm : 0.0f;
      float len = 2.8f * (0.5f + 0.5f * a);
      float wid = 1.8f * (0.5f + 0.5f * a);
      float px_ = -dy * wid, py_ = dx * wid;
      float hx = dx * len, hy = dy * len;
      mb.color(1.0f, 0.45f + 0.45f * a, 0.2f, a);
      mb.vertex(x + hx, y + hy); mb.vertex(x + px_, y + py_); mb.vertex(x - hx, y - hy);
      mb.vertex(x + hx, y + hy); mb.vertex(x - hx, y - hy); mb.vertex(x - px_, y - py_);
    }
    mb.end();
    mesh.upload(mb, GL_DYNAMIC_DRAW);
    mesh.draw();
  }
}

Save::Station GLStation::capture_state() const {
  Save::Station s;
  s.present = true;
  s.alive = alive;
  s.lives = lives;
  s.health = health;
  s.pos_x = position.x();
  s.pos_y = position.y();
  s.vel_x = velocity.x();
  s.vel_y = velocity.y();
  s.inner_rotation = inner_rotation;
  s.outer_rotation = outer_rotation;
  s.wave = wave;
  s.difficulty = difficulty;
  s.ships_this_wave = ships_this_wave;
  s.ships_left_to_deploy = ships_left_to_deploy;
  s.time_until_next_ship = time_until_next_ship;
  s.deploying = deploying;
  s.redeploying = redeploying;
  for (const auto *gs : *objects) {
    // Skip enemies that are dead but not yet pruned (the host keeps them a
    // couple of seconds for debris). Serializing them would make the
    // client — which wholesale-rebuilds this list from the record 10x/s —
    // resurrect a killed enemy for that whole window; its death visual
    // already replicates via EV_WORLD_BOOM.
    if (!gs->ship->is_alive()) continue;
    Save::Enemy e;
    e.pos_x = gs->ship->position.x();
    e.pos_y = gs->ship->position.y();
    e.vel_x = gs->ship->velocity.x();
    e.vel_y = gs->ship->velocity.y();
    e.facing_x = gs->ship->facing.x();
    e.facing_y = gs->ship->facing.y();
    e.thrust_force = gs->ship->thrust_force;
    e.rotation_force = gs->ship->rotation_force;
    e.value = gs->ship->value;
    s.enemies.push_back(e);
  }
  return s;
}

void GLStation::restore_state(const Save::Station &s, const Grid &grid) {
  // Net client: the host's copy just died — run the same debris burst its
  // destroy() made, or the replica silently pops out of existence (the
  // boom sound arrives separately via EV_STATION_BOOM).
  if (alive && !s.alive) destroy();
  alive = s.alive;
  lives = s.lives;
  health = s.health;
  position = WrappedPoint(s.pos_x, s.pos_y);
  velocity = Point(s.vel_x, s.vel_y);
  inner_rotation = s.inner_rotation;
  outer_rotation = s.outer_rotation;
  wave = s.wave;
  difficulty = s.difficulty;
  ships_this_wave = s.ships_this_wave;
  ships_left_to_deploy = s.ships_left_to_deploy;
  time_until_next_ship = (float)s.time_until_next_ship;
  deploying = s.deploying;
  redeploying = s.redeploying;
  // Reconcile the deployed enemies IN PLACE against the record, in list
  // order (the net extras re-stamp pairs wire ids to replicas by the
  // same order). The old wholesale delete+recreate ran on every 10 Hz
  // net apply, and each GLEnemy construction builds and uploads fresh
  // GPU meshes — at gen-20+ wave sizes that alone held the client near
  // 15 fps (Glenn's level-21 frame hitches). A save-load lands here
  // with an empty list, where reconcile degenerates to the old appends.
  std::list<GLShip *>::iterator oi = objects->begin();
  for (const auto &se : s.enemies) {
    GLEnemy *ge;
    if (oi != objects->end()) {
      ge = (GLEnemy *)*oi;
      // Order-based pairing shifts when deaths shuffle the list mid-wave.
      // Stats are overwritten every apply, but the mesh/colour/trail were
      // chosen at construction — left alone, a green arrowhead could fly
      // bomber behaviour for the rest of the wave. Re-derive the variant
      // from the record's thrust band and rebuild the replica on mismatch
      // (rare: only when the pairing actually shifted, so the 10 Hz cost
      // that killed wholesale delete+recreate never returns).
      GLEnemy::Variant want = GLEnemy::variant_for_thrust(se.thrust_force);
      if (ge->variant() != want) {
        if (missile_ships) missile_ships->remove(ge->ship);
        delete ge;
        ge = new GLEnemy(grid, se.pos_x, se.pos_y, targets, (float)difficulty,
                         asteroids, aim_lead, want);
        if (missile_ships) missile_ships->push_back(ge->ship);
        if (!ge->ship->behaviours.empty())
          if (Follower *f = dynamic_cast<Follower*>(ge->ship->behaviours.front()))
            f->lock_now();
        *oi = ge;
      }
      ++oi;
    } else {
      // Re-derive the variant from the stats the record already carries
      // (glenemy.h: the interceptor IS its stats, no serialized flag).
      ge = new GLEnemy(grid, se.pos_x, se.pos_y, targets, (float)difficulty,
                       asteroids, aim_lead,
                       GLEnemy::variant_for_thrust(se.thrust_force));
      objects->push_back(ge);
      if (missile_ships) missile_ships->push_back(ge->ship);
      // Skip the initial 2.5s lock delay — enemy is already deployed at
      // the recorded position.
      if (!ge->ship->behaviours.empty())
        if (Follower *f = dynamic_cast<Follower*>(ge->ship->behaviours.front()))
          f->lock_now();
      oi = objects->end();
    }
    ge->ship->alive = true;
    ge->ship->position = WrappedPoint(se.pos_x, se.pos_y);
    ge->ship->velocity = Point(se.vel_x, se.vel_y);
    ge->ship->facing = Point(se.facing_x, se.facing_y);
    ge->ship->thrust_force = se.thrust_force;
    ge->ship->rotation_force = se.rotation_force;
    ge->ship->value = se.value;
  }
  // Record shrank (deaths the host already pruned): drop the leftovers.
  while (oi != objects->end()) {
    if (missile_ships) missile_ships->remove((*oi)->ship);
    delete *oi;
    oi = objects->erase(oi);
  }
}

void GLStation::net_client_step(float delta) {
  // CompositeObject::step = Object::step + the debris loop, so the death
  // burst animates on the client too (plain Object::step left the burst
  // frozen at its spawn points — the particles never stepped).
  CompositeObject::step((int)delta);
  if (!alive) return;
  outer_rotation += outer_rotation_speed * delta;
  inner_rotation += inner_rotation_speed * delta;
}

void GLStation::step(float delta, const Grid &grid) {
  Ship::step(delta, grid);
  if (!alive) return;
  outer_rotation += outer_rotation_speed * delta;
  inner_rotation += inner_rotation_speed * delta;
  if(deploying) {
    time_until_next_ship -= delta;
    if(ships_left_to_deploy == 0) {
      deploying = false;
      ships_this_wave += extra_ships_per_wave;
      if(ships_this_wave > max_ships_per_wave) {
        ships_this_wave = max_ships_per_wave;
        if(!redeploying) {
          difficulty++;
        } else {
          redeploying = false;
        }
      }
    } else if (time_until_next_ship <= 0) {
      time_until_next_ship += time_between_ships;
      ships_left_to_deploy--;
      // Interceptors launch FIRST — a fast vanguard ahead of the slow
      // line gives each wave a shape (and they would arrive first
      // anyway) — rammers charge out right behind them, and bombers
      // launch LAST, the rear line settling in behind everyone. Deploy
      // order counts up from 0 as ships_left counts down; the caps
      // guarantee the allotments never overlap.
      int deploy_index = ships_this_wave - 1 - ships_left_to_deploy;
      // reset() converts survivors into EXTRA deploy slots, so ships_left
      // can exceed the wave size and the index goes negative — and a
      // negative index satisfies `< wave_interceptors()`, redeploying
      // every carried slot as the fastest hull. Carried slots are plain
      // reinforcements: deal them as standards.
      GLEnemy::Variant variant =
          deploy_index < 0                                  ? GLEnemy::STANDARD
        : deploy_index < wave_interceptors()                ? GLEnemy::INTERCEPTOR
        : deploy_index < wave_interceptors() + wave_rammers() ? GLEnemy::RAMMER
        : deploy_index >= ships_this_wave - wave_bombers()  ? GLEnemy::BOMBER
                                                            : GLEnemy::STANDARD;
      float rotation = 360.0/ships_this_wave*ships_left_to_deploy*M_PI/180;
      float distance = 30 + radius;
      GLEnemy *deployed = new GLEnemy(
          grid,
          position.x() + distance*cos(rotation),
          position.y() + distance*sin(rotation), targets, difficulty, asteroids,
          aim_lead, variant);
      objects->push_back(deployed);
      if(missile_ships) missile_ships->push_back(deployed->ship);
    }
  } else if (objects->empty()) {
    deploying = true;
    wave++;
    time_until_next_ship = 0.0;
    ships_left_to_deploy = ships_this_wave;
  }
}
