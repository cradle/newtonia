#ifndef TELEPORT_H
#define TELEPORT_H

#include "behaviour.h"

class Ship;
class Grid;

class Teleport : public Behaviour {
public:
  Teleport(Ship *ship, const Grid &grid) : Behaviour(ship), grid(grid) {};
  virtual ~Teleport() {};
  virtual void step(int delta);
private:
  const Grid &grid;
};

#endif