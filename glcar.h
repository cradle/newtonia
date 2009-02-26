#ifndef GL_CAR_H
#define GL_CAR_H

#include "glship.h"
#include "ship.h"

class GLCar : public GLShip {
public:
  GLCar(bool has_friction);
  ~GLCar();
  
protected:
  GLuint left_jet, right_jet;
  
  void draw_ship(bool minimap) const;
};

#endif
