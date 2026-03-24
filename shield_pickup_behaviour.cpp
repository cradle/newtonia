#include "shield_pickup_behaviour.h"
#include "ship.h"

ShieldPickupBehaviour::ShieldPickupBehaviour(Ship *ship, int amount)
  : Behaviour(ship), amount(amount) {}

void ShieldPickupBehaviour::step(int delta) {
  ship->add_shield_ammo(amount);
  done = true;
}
