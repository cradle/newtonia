#ifndef CAR_H
#define CAR_H

#include "ship.h"

class Car : public Ship {
  public:
    Car() {};
    Car(float x, float y);
    
    void step(float delta);
};

#endif
