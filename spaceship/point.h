#ifndef POINT_H
#define POINT_H
#include <iostream>

class Point {
  public:
    Point();
    Point(const Point& other);
    Point(float x, float y);

    void rotate(float radians);
    float direction();
    float magnitude();

    void operator+=(const Point other);
    Point operator*(float scalar);
    Point operator/(float scalar);
    Point operator+(Point other);
    Point operator-(Point other);

  friend std::ostream& operator << (std::ostream& os, const Point& p);

  //TODO: Make friend/private for view
    float x;
    float y;
};

#endif /* POINT_H */
