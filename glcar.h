#ifndef GL_CAR_H
#define GL_CAR_H

#include "glship.h"
#include "car.h"

class GLCar : public GLShip {
public:
  GLCar(float x, float y);
  virtual ~GLCar();
  
protected:
  GLuint left_jet, right_jet;
  
  void draw_ship(bool minimap) const;
};

#endif
