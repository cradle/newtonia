#ifndef POINT_H
#define POINT_H
#include <iostream>

class Point {
  public:
    Point();
    Point(const Point& other);
    Point(float x, float y);

    void rotate(float radians);
    Point normalized();
    Point perpendicular();
    float direction();
    float magnitude();
    float magnitude_squared();

    void operator+=(const Point other);
    Point operator*(float scalar);
    Point operator/(float scalar);
    Point operator+(Point other);
    Point operator-(Point other);
    
    operator const float* ();

  friend std::ostream& operator << (std::ostream& os, const Point& p);

    float x() const;
    float y() const;
    
  protected:
    static const int X = 0, Y = 1;
    float coords[2];
};

inline
float Point::x() const {
  return coords[X];
}
//TODO: Make everything const that should be
inline
float Point::y() const {
  return coords[Y];
}

inline
Point::operator const float*() {
  return coords;
}

inline
void Point::operator+=(const Point other) {
  coords[X] += other.coords[X];
  coords[Y] += other.coords[Y];
}

inline
Point Point::operator+(Point other) {
  return Point(coords[X] + other.coords[X], coords[Y] + other.coords[Y]);
}

inline
Point Point::operator-(Point other) {
  return Point(coords[X] - other.coords[X], coords[Y] - other.coords[Y]);
}

//TODO: Write tests
inline
Point Point::operator*(float scalar) {
  return Point(coords[X] * scalar, coords[Y] * scalar);
}

inline
Point Point::operator/(float scalar) {
  return Point(coords[X] / scalar, coords[Y] / scalar);
}

#endif /* POINT_H */
