#include "teleport.h"
#include "wrapped_point.h"
#include "ship.h"

void Teleport::step(int delta) {
  ship->explode();
  ship->position = WrappedPoint();
  ship->explode();
  done = true;
}