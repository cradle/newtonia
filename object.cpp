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
  // Direct (non-grid) tests must respect the toroidal world: compare against
  // the copy of `other` nearest to us, or objects straddling the wrap seam —
  // e.g. the station, which spawns on it at (0,0) — miss collisions the tiled
  // renderer plainly shows. Grid queries pass explicit wrap offsets instead
  // (Grid::collide), so the 3-arg overload below stays untouched.
  float r = radius + other.effective_radius() + proximity;
  Point o_near = other.position.closest_to(position);
  return (o_near - position).magnitude_squared() < r * r;
}

bool Object::collide(const Object &other, float proximity, const Point offset) const {
  float r = radius + other.effective_radius() + proximity;
  return ((other.position + offset) - position).magnitude_squared() < r * r;
}

bool Object::contains(Point /*p*/, float /*r*/) const {
  return true; // default: circle hit is sufficient
}

bool Object::kill() {
  if(!invincible && alive) {
    alive = false;
    return true;
  }
  return false;
}
