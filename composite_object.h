#ifndef COMPOSITE_OBJECT_H
#define COMPOSITE_OBJECT_H

#include <list>
#include "particle.h"

class CompositeObject : public Object {
public:
  virtual ~CompositeObject() {};
  virtual void step(int delta);
  virtual bool is_removable() const;
  virtual bool kill();
  void explode();
  void explode(Point const position, Point const velocity);
  
protected:
  list<Particle> debris;
  
  //FIX: encapsulation
  // friend class GLShip;
  friend class Ship;
  // friend class GLGame;
  // friend class GLTrail;
  // friend class Enemy;
};

#endif
