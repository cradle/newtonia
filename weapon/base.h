#ifndef BASE_H
#define BASE_H

#include <string>
using namespace std;
class Ship;

namespace Weapon {
  class Base {
  public:
    Base(Ship *ship) : ship(ship), _name("unnamed"), shooting(false), unlimited(true) {}
    virtual ~Base() {};
  
    const char* name() const;
    virtual void shoot(bool on = true) = 0;
    virtual void step(int delta) = 0;
    bool is_shooting() const;
    bool is_unlimited() const;
    bool empty() const;
    int ammo() const;
    
  protected:
    Ship *ship;
    int _ammo;
    string _name;
    bool shooting, unlimited;
  
  private:
    Base() {};
  };
  
  inline const char* Base::name() const {
    return _name.c_str();
  }
  inline bool Base::is_shooting() const {
    return shooting;
  }
  inline bool Base::is_unlimited() const {
    return unlimited;
  }
  inline bool Base::empty() const {
    return !unlimited && _ammo == 0;
  }
  inline int Base::ammo() const {
    return _ammo;
  }
}

#endif