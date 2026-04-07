#ifndef GL_CAR_H
#define GL_CAR_H

#include "glship.h"
#include "mesh.h"
#include "ship.h"

class GLCar : public GLShip {
public:
  GLCar(const Grid &grid, bool has_friction);
  virtual ~GLCar();

protected:
  Mesh left_jet, right_jet;

  void draw_ship(bool minimap) const;
};

#endif
