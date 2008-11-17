#ifndef CAR_H
#define CAR_H

#include "ship.h"

class Car : public Ship {
  public:
    Car() {};
    Car(float x, float y);
    
    virtual void step(float delta);
};

#endif
