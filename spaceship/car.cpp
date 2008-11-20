#include "car.h"

#include <iostream>

Car::Car(float x, float y) : Ship(x,y) {
  thrust_force = 0.1;
  rotation_force = 0.2;
}

void Car::step(float delta) {
  Ship::step(delta);
  if(is_alive()) {
    velocity = velocity - velocity * 0.0025 * delta;
  }
}
