#ifndef GL_STATION_H
#define GL_STATION_H

#include <vector>
#include "glship.h"

class GLStation {
public:
  GLStation(std::vector<GLShip*>* objects);
  void draw();
  void step(float delta);
  
private:
  unsigned int body;
  static const int NUM_SEGMENTS = 30;
  float inner_rotation, outer_rotation, outer_rotation_speed, inner_rotation_speed;
  
  std::vector<GLShip*>* objects;
  float time_between_waves, time_until_next_wave;
};

#endif