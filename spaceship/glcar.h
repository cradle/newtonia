#ifndef GL_CAR_H
#define GL_CAR_H

#include "glship.h"
#include "car.h"

class GLCar : public GLShip {
public:
  GLCar(float x, float y);
  void input(unsigned char key, bool pressed);
  
protected:
  void draw_ship();
  void draw_trails();
};

#endif
