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

bool Object::collide(Object *other, float proximity) {
  // TODO: should be using "other->position.closest_to(position)", but closest_to is
  // FIX: very very slow. either optimise closest_to or fix some other way
  // TODO: benchmark/profile this 'moving in direction of' test ? ((other->position - position) * (other.velocity - velocity)).sum()
  if ((other->position.x() - position.x()) * (velocity.x() - other->velocity.x()) + \
      (other->position.y() - position.y()) * (velocity.y() - other->velocity.y()) <= 0)
    return false;
  return ((other->position - position).magnitude_squared() < ((radius+other->radius+proximity)*(radius+other->radius+proximity)));
}

bool Object::kill() {
  if(!invincible) {
    alive = false; 
    return true;
  }
  return false;
}
