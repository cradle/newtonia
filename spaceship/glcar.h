#ifndef GL_CAR_H
#define GL_CAR_H

#include "glship.h"
#include "car.h"

class GLCar : public GLShip {
public:
  GLCar(float x, float y);
  void draw();
  void step(float delta);
  
  std::deque<std::deque<Bullet*>*> trails2;
};

#endif
