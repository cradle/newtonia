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
  GLStation(const Grid &grid, list<GLShip*>* objects, list<GLShip*>* targets, list<Object*>* asteroids = NULL);
  virtual ~GLStation();

  void draw(bool minimap = false) const;
  void step(float delta, const Grid &grid);
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
};

#endif
