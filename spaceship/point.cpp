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
  return Point(y(), -x());
}

Point Point::normalized() {
  return *this / magnitude();
}

float Point::magnitude() {
  return sqrt(magnitude_squared());
}

float Point::magnitude_squared() {
  return x()*x() + y()*y();
}

float Point::direction() {
  return atan2(y(),x()) * 180.0 / M_PI - 90.0;
}

void Point::rotate(float radians) {
  //TODO: Must remain normalised
  float oldx = x();
  float oldy = y();
  coords[X] = oldx * cos(radians) - oldy * sin(radians);
  coords[Y] = oldy * cos(radians) + oldx * sin(radians);
}

void Point::operator+=(const Point other) {
  coords[X] += other.coords[X];
  coords[Y] += other.coords[Y];
}

Point Point::operator+(Point other) {
  return Point(x() + other.x(), y() + other.y());
}

Point Point::operator-(Point other) {
  return Point(x() - other.x(), y() - other.y());
}

//TODO: Write tests
Point Point::operator*(float scalar) {
  return Point(x() * scalar, y() * scalar);
}

Point Point::operator/(float scalar) {
  return Point(x() / scalar, y() / scalar);
}
