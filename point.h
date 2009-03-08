#ifndef POINT_H
#define POINT_H
#include <iostream>

class Point {
  public:
    Point();
    Point(const Point& other);
    Point(float x, float y);
    
    float x() const;
    float y() const;

    void zero();
    void rotate(float radians);
    
    Point normalized() const;
    Point perpendicular() const;
    float direction() const;
    float magnitude() const;
    float magnitude_squared() const;

    void operator+=(const Point other);
    Point operator*(float scalar) const;
    Point operator/(float scalar) const;
    Point operator/(const Point other) const;
    Point operator+(const Point other) const;
    Point operator-(const Point other) const;
    
    operator const float* () const;

    friend std::ostream& operator << (std::ostream& os, const Point& p);
    
  protected:
    enum points { X = 0, Y = 1 };
    float coords[2];
};

#endif /* POINT_H */
