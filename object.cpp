#include "object.h"
#include <list>
#include "wrapped_point.h"
#include <iostream>

using namespace std;

Object::Object() {
  commonInit();
}

Object::Object(WrappedPoint position, Point velocity) :
  position(position), velocity(velocity) {
  //Annoyingly horrible workaround
  commonInit();
}

//FIX: When compiler supports target/delegate constructos
// USE THEM
void Object::commonInit() {
  radius = 1.0f;
  rotation = 0.0f;
  alive = true;
  rotation_speed = 0.0f;
  value = 0;
  friction = 0.0f;
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
  Point posDelta = other->position - position;
  float posMag = posDelta.magnitude_squared();
  float radSum = radius + other->radius;
  float radSquard = radSum * radSum;
  return ((other->position - position).magnitude_squared() < ((radius+other->radius)*(radius+other->radius)));
}

void Object::kill() {
  alive = false;
}
