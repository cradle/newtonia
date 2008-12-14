#include "object.h"
#include "wrapped_point.h"

Object::Object() {
  radius = radius_squared = 1.0f;
}
  
void Object::step(int delta) {
  position += velocity * delta;
  position.wrap();
}
  
bool Object::collide(Object const other) const {
  return ((other.position - position).magnitude_squared() < (radius_squared + other.radius_squared));
}