#ifndef SHIELD_PICKUP_BEHAVIOUR_H
#define SHIELD_PICKUP_BEHAVIOUR_H

#include "behaviour.h"

class ShieldPickupBehaviour : public Behaviour {
public:
  ShieldPickupBehaviour(Ship *ship, int amount);
  virtual ~ShieldPickupBehaviour() {};
  virtual void step(int delta);

private:
  int amount;
};

#endif
