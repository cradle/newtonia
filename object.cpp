#include "object.h"
#include <list>
#include "wrapped_point.h"
#include <iostream>

using namespace std;

Object::Object() {
  commonInit();
}

Object::Object(WrappedPoint position, Point velocity, float rotation_speed) :
  position(position), velocity(velocity), rotation_speed(rotation_speed) {
  //Annoyingly horrible workaround
  commonInit();
}

//FIX: When compiler supports target/delegate constructos
// USE THEM
void Object::commonInit() {
  radius = 1.0f;
  rotation = 0.0f;
  alive = true;
  value = 0;
  friction = 0.0f;
  invincible = false;
}

bool Object::is_removable() const {
  return !alive;
}

void Object::step(int delta) {
  position += velocity * delta;
  position.wrap();
  rotation += rotation_speed * delta;
}

bool Object::collide(const Object &other, float proximity) const {
  return collide(other, proximity, Point(0,0));
}

bool Object::collide(const Object &other, float proximity, const Point offset) const {
  return ((other.position + offset) - position).magnitude_squared() < ((radius+other.radius+proximity)*(radius+other.radius+proximity));
}

bool Object::kill() {
  if(!invincible && alive) {
    alive = false;
    return true;
  }
  return false;
}
