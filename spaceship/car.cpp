#include "car.h"

#include <iostream>

Car::Car(float x, float y) : Ship(x,y) {
  thrust_force = 0.1;
}

void Car::step(float delta) {
  std::cout << "car stepping" << std::endl;
  Ship::step(delta);
  velocity = velocity - velocity * 0.025 * delta;
  // 0.5*velocity.magnitude()*velocity.magnitude()*velocity.direction() * delta;
}
