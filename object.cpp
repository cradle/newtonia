#include "object.h"
#include "wrapped_point.h"
#include <iostream>

Object::Object() : radius(1.0f), rotation(0.0f), alive(true), rotation_speed(0.0f) {}

Object::Object(WrappedPoint position, Point velocity) : 
  position(position), velocity(velocity) {
  Object();
  //position(WrappedPoint(position)), velocity(Point(velocity)),
}

bool Object::is_removable() const {
  return !alive;
}
  
void Object::step(int delta) {
  position += velocity * delta;
  position.wrap();
  rotation += rotation_speed * delta;
}
  
bool Object::collide(Object *other) {
  // difference_in_position * difference_in_velocity <= 0
  // if (ball.loc[0] - self.loc[0]) * (self.dx - ball.dx) + \
  //    (ball.loc[1] - self.loc[1]) * (self.dy - ball.dy) <= 0:
  if(this == other) return false;
  if((other->position - position).magnitude_squared() < ((radius+other->radius)*(radius+other->radius))) {
    alive = false;
    other->alive = false;
    return true;
  } else {
    return false;
  }
}