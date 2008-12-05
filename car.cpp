#include "car.h"

#include <iostream>

Car::Car(float x, float y) : Ship(x,y) {
  reverse_force = -0.05;
  thrust_force = 0.09;
  rotation_force = 0.2;
  heat_rate = 0.04;
  cool_rate = 0.03;
}

void Car::step(float delta) {
  Ship::step(delta);
  if(is_alive()) {
    velocity = velocity - velocity * 0.001 * delta;
  }
}
