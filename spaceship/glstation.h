#ifndef GL_STATION_H
#define GL_STATION_H

#include <vector>
#include "glship.h"
#include "wrapped_point.h"

using namespace std;

class GLStation {
public:
  GLStation(vector<GLShip*>* objects, vector<GLShip*>* targets);
  void draw();
  void step(float delta);
  void collide(Ship *ship);
  
private:
  float radius, radius_squared;
  WrappedPoint position;
  unsigned int body;
  static const int NUM_SEGMENTS = 30;
  float inner_rotation, outer_rotation, outer_rotation_speed, inner_rotation_speed;
  
  std::vector<GLShip*>* objects;
  std::vector<GLShip*>* targets;
  int ships_this_wave, max_ships_per_wave, ships_left_to_deploy;
  float time_until_next_ship, time_between_ships;
  bool deploying;
};

#endif