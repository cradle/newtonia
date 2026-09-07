#include "teleport.h"
#include "wrapped_point.h"
#include "ship.h"

void Teleport::step(int delta) {
  done = true;
  Point departure = ship->position;
  if (!ship->find_teleport_destination(grid)) return;
  WrappedPoint destination = ship->position;
  ship->position = WrappedPoint(departure.x(), departure.y());
  ship->explode();
  Ship::play_teleport_sound(departure);
  Ship::teleport_events.push_back(std::make_pair(ship->net_seat, departure));
  ship->net_teleport_count++;
  ship->net_warp_count++;  // discontinuous move — netplay clients must not blend
  ship->position = destination;
  ship->explode(ship->position, Point());
  if(ship->time_left_invincible < 1000) {
    ship->time_left_invincible = 1000;
    ship->invincible = true;
  }
}
