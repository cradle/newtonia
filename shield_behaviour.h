#ifndef SHIELD_BEHAVIOUR_H
#define SHIELD_BEHAVIOUR_H

#include "behaviour.h"

class ShieldBehaviour : public Behaviour {
public:
  ShieldBehaviour(Ship *ship, int duration);
  virtual ~ShieldBehaviour() {};
  virtual void step(int delta);
};

#endif
