#ifndef BASE_H
#define BASE_H

#include <string>
using namespace std;
class Ship;

namespace Weapon {
  class Base {
  public:
    Base(Ship *ship) : ship(ship), _name("unnamed") {}
    virtual ~Base() {};
  
    const char* name() const;
    virtual void shoot(bool on = true) = 0;
    virtual void step(int delta) = 0;
    
  protected:
    Ship *ship;
    string _name;
  
  private:
    Base() {};
  };
  
  inline const char* Base::name() const {
    return _name.c_str();
  }
}

#endif