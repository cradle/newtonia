#include "point.h"

#include <iostream>
#include <math.h>

using namespace std;

ostream& operator << (ostream& os, const Point& p)
{
  return os << "(x,y): (" << p.x << "," << p.y << ")";
}

Point::Point() {
  x = 0;
  y = 0;
}

Point::Point(const Point& other) {
  x = other.x;
  y = other.y;
}

Point::Point(float x, float y) {
  this->x = x;
  this->y = y;
}

float Point::direction() {
  return atan2(y,x) * 180.0 / M_PI - 90.0;
}

void Point::rotate(float radians) {
  //TODO: implement rotation speed, make use timestep
  //TODO: Must remain normalised. Is lossy? (floats?)
  float oldx = x;
  float oldy = y;
  x = oldx * cos(radians) - oldy * sin(radians);
  y = oldy * cos(radians) + oldx * sin(radians);
}

void Point::operator+=(const Point other) {
  x += other.x;
  y += other.y;
}

Point Point::operator+(Point other) {
  return Point(x + other.x, y + other.y);
}

//TODO: Write tests
Point Point::operator*(float scalar) {
  return Point(x * scalar, y * scalar);
}

Point Point::operator/(float scalar) {
  return Point(x / scalar, y / scalar);
}
