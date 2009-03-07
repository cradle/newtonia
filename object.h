#ifndef OBJECT_H
#define OBJECT_H

#include <list>
#include "wrapped_point.h"

using namespace std;

class Object {
public:
  Object();
  Object(WrappedPoint position, Point velocity);
  virtual ~Object() {};
  virtual void step(int delta);
  virtual bool collide(const Object &other, float proximity = 0.0f) const;
  virtual bool is_removable() const;
  void commonInit();
  bool is_alive() const;
  long long get_value() const;

  //TODO: work out friend methods ::draw
  //TODO: work out inheritance with static, OR, work out borg?
  friend class AsteroidDrawer;
  friend class Behaviour;
  
  //TODO: Fix encapsulation
  WrappedPoint position;
  Point velocity;
  
  virtual bool kill();
  float radius, radius_squared;
  long long value;
  float rotation, rotation_speed, friction;
  bool alive, invincible;
};

inline
bool Object::is_alive() const {
  return alive;
}

inline
long long Object::get_value() const {
  return value;
}

#endif
