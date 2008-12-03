#ifndef GL_STATION_H
#define GL_STATION_H

#include <list>
#include "glship.h"
#include "wrapped_point.h"

using namespace std;

class GLStation {
public:
  GLStation(list<GLShip*>* objects, list<GLShip*>* targets);
  void draw(bool minimap = false) const;
  void step(float delta);
  void collide(Ship *ship) const;
  int level() const;
  
private:
  float radius, radius_squared;
  WrappedPoint position;
  unsigned int body, map_body;
  static const int NUM_SEGMENTS = 30;
  float inner_rotation, outer_rotation, outer_rotation_speed, inner_rotation_speed;
  
  std::list<GLShip*>* objects, *targets;
  int ships_this_wave, max_ships_per_wave, extra_ships_per_wave, ships_left_to_deploy;
  float time_until_next_ship, time_between_ships;
  bool deploying;
  int wave, difficulty;
};

#endif