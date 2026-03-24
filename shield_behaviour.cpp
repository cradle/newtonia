#include "shield_behaviour.h"
#include "ship.h"

ShieldBehaviour::ShieldBehaviour(Ship *ship, int duration)
  : Behaviour(ship) {
  ship->invincible = true;
  ship->time_left_invincible = duration;
  ship->set_shield_hum(true);
}

void ShieldBehaviour::step(int delta) {
  if (ship->time_left_invincible <= 0) {
    ship->set_shield_hum(false);
    done = true;
  }
}
