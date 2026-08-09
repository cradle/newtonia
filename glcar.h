#ifndef GL_CAR_H
#define GL_CAR_H

#include "glship.h"
#include "mesh.h"
#include "ship.h"

class GLCar : public GLShip {
public:
  // tint: optional seat colour (FOURPLAYER.md D7) applied BEFORE the body
  // meshes bake; NULL keeps the classic orange. The minimap dot and lives
  // icons read `color` live, so they follow automatically.
  GLCar(const Grid &grid, bool has_friction, const float *tint = NULL);
  virtual ~GLCar();

protected:
  Mesh left_jet, right_jet;

  void draw_ship(bool minimap) const;
};

#endif
