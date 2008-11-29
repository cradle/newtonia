#ifndef GL_STATION_H
#define GL_STATION_H

class GLStation {
public:
  GLStation();
  void draw();
  void step(float delta);
  
private:
  unsigned int body;
  static const int NUM_SEGMENTS = 30;
  float inner_rotation, outer_rotation, outer_rotation_speed, inner_rotation_speed;
};

#endif