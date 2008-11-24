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
    float direction();
    float magnitude();

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

#endif /* POINT_H */
