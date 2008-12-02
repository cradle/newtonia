#include "point.h"

#include <iostream>
#include <math.h>

using namespace std;

ostream& operator << (ostream& os, const Point& p)
{
  return os << "(x,y): (" << p.x() << "," << p.y() << ")";
}

Point::Point() {
  coords[X] = 0;
  coords[Y] = 0;
}

Point::Point(const Point& other) {
  coords[X] = other.coords[X];
  coords[Y] = other.coords[Y];
}

Point::Point(float x, float y) {
  coords[X] = x;
  coords[Y] = y;
}

Point Point::perpendicular() {
  return Point(coords[Y], -coords[X]);
}

Point Point::normalized() {
  return *this / magnitude();
}

float Point::magnitude() {
  return sqrt(magnitude_squared());
}

float Point::magnitude_squared() {
  return coords[X]*coords[X] + coords[Y]*coords[Y];
}

float Point::direction() {
  return atan2(coords[Y],coords[X]) * 180.0 / M_PI - 90.0;
}

void Point::rotate(float radians) {
  //TODO: Must remain normalised
  float oldx = coords[X];
  float oldy = coords[Y];
  coords[X] = oldx * cos(radians) - oldy * sin(radians);
  coords[Y] = oldy * cos(radians) + oldx * sin(radians);
}
