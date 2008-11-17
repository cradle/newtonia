#include "car.h"

Car::Car(float x, float y) : Ship(x,y) {
  thrust_force = 0.1;
}

void Car::step(float delta) {
  Ship::step(delta);
  velocity = velocity - velocity * 0.0025 * delta;
  // 0.5*velocity.magnitude()*velocity.magnitude()*velocity.direction() * delta;
}