#ifndef CAR_H
#define CAR_H

#include "ship.h"

class Car : public Ship {
  public:
    Car() {};
    Car(float x, float y);
    virtual ~Car() {};
    
    virtual void step(float delta);
};

#endif
