#ifndef GLENEMY_H
#define GLENEMY_H

#include "glship.h"
#include <vector>

class GLEnemy : public GLShip {
public:
  GLEnemy(float x, float y, std::vector<GLShip*> * target);
};

#endif