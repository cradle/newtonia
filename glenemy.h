#ifndef GLENEMY_H
#define GLENEMY_H

#include "glship.h"
#include <list>

class GLEnemy : public GLShip {
public:
  GLEnemy(const Grid &grid, float x, float y, std::list<GLShip*> * target, float difficulty = 1);
  virtual ~GLEnemy();
};

#endif
