#ifndef TELEPORT_H
#define TELEPORT_H

#include "behaviour.h"

class Ship;

class Teleport : public Behaviour {
public:
  Teleport(Ship *ship) : Behaviour(ship) {};
  virtual ~Teleport() {};
  virtual void step(int delta);
};

#endif