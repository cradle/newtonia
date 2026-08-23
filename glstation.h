#ifndef GL_STATION_H
#define GL_STATION_H

#include <list>
#include "glship.h"
#include "mesh.h"
#include "object.h"
#include "wrapped_point.h"
#include "savegame.h"

using namespace std;

class GLStation : public Ship {
public:
  GLStation(const Grid &grid, list<GLShip*>* objects, list<GLShip*>* targets, list<Object*>* asteroids = NULL, float aim_lead = 0.0f, int generation = 0);
  virtual ~GLStation();

  void draw(bool minimap = false) const;
  void step(float delta, const Grid &grid);
  // Net client: motion + ring spin only — deployment, waves and
  // collisions are host-authoritative (restore_state mirrors them).
  void net_client_step(float delta);
  // void collide(Ship *ship) const;
  void reset(bool was_killed = true);
  int level() const;
  void hit();
  void destroy();

  Save::Station capture_state() const;
  void restore_state(const Save::Station &s, const Grid &grid);

  int health;

private:
  Mesh body_mesh, map_body_mesh;
  static const int NUM_SEGMENTS = 30;
  float inner_rotation, outer_rotation, outer_rotation_speed, inner_rotation_speed;

  std::list<GLShip*>* objects, *targets;
  std::list<Object*>* asteroids;
  int ships_this_wave, max_ships_per_wave, extra_ships_per_wave, ships_left_to_deploy;
  int time_until_next_ship, time_between_ships;
  bool deploying, redeploying;
  int wave, difficulty;
  // How much of the perfect intercept lead this generation's wave ships aim
  // with (GLGame::hostile_aim_lead; the station is recreated each rollover,
  // so the value tracks the generation with no saved state). Handed to each
  // deployed GLEnemy's Follower at spawn.
  float aim_lead;
  // The generation this station was built for (same recreated-per-rollover
  // contract as aim_lead — never persisted). Drives wave COMPOSITION:
  // interceptors join from generation 15, their per-wave allotment growing
  // with it (wave_interceptors). NOT the `difficulty` member below — that
  // counter only moves past the 50-ship wave cap and resets with the
  // station every level, so it never actually climbs.
  int generation;
  // How many of this wave's ships deploy as interceptors: none before
  // gen 15, then 1 + (generation - 15), capped at half the wave (rounded
  // up) so the standard line always outnumbers-or-matches the vanguard.
  int wave_interceptors() const;
  // How many of this wave's ships deploy as bombers: none before gen 17,
  // then 1 + (generation - 17), capped so at least one standard ship
  // always sits between the interceptor vanguard and the bomber rear line
  // (which keeps small opening waves bomber-free).
  int wave_bombers() const;
};

#endif
