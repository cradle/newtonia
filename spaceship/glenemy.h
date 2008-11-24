#ifndef GLENEMY_H
#define GLENEMY_H

#include "glship.h"

class GLEnemy : public GLShip {
public:
  GLEnemy(float x, float y, GLShip* target);
};

#endif