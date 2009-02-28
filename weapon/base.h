#ifndef BASE_H
#define BASE_H

class Ship;

namespace Weapon {
  class Base {
  public:
    Base(Ship *ship) : ship(ship) {}
    virtual ~Base() {};
  
    virtual void shoot(bool on = true) = 0;
    virtual void step(int delta) = 0;
    
  protected:
    Ship *ship;
  
  private:
    Base() {};
  };
}

#endif