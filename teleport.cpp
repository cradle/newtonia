#include "teleport.h"
#include "wrapped_point.h"
#include "ship.h"

void Teleport::step(int delta) {
  ship->explode();
  ship->net_warp_count++;  // discontinuous move — netplay clients must not blend
  ship->position = WrappedPoint();
  ship->explode(ship->position, Point());
  if(ship->time_left_invincible < 1000) {
    ship->time_left_invincible = 1000;
    ship->invincible = true;
  }
  done = true;
}