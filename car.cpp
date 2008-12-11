#include "car.h"

#include <iostream>

Car::Car(float x, float y) : Ship(x,y) {
  friction = 0.001;
  reverse_force = -0.05;
  thrust_force = 0.09;
  rotation_force = 0.2;
  heat_rate = 0.035;
  retro_heat_rate = heat_rate * -reverse_force / thrust_force;
  cool_rate = retro_heat_rate * 0.9;
}