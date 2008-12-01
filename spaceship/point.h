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

#endif /* POINT_H */
