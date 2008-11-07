#include <iostream>

using namespace std;

class Point {
  public:
    Point();
    Point(int x, int y);
    void puts();
  private:
    int x;
    int y;
};

Point::Point() {
  // TODO: why do I need this constructor?
  this->x = 0;
  this->y = 0;
}

Point::Point(int x, int y) {
  this->x = x;
  this->y = y;
}

void Point::puts() {
  // TODO: Work out how to overide cout << 
  cout << "(x,y): (" << this->x << "," << this->y << ")";
}

class Ship {
  public:
    Ship(int x, int y);
    void fly();

  private:
    Point position;
    Point facing;
    Point velocity;
};

Ship::Ship(int x, int y) {
  position = Point(x, y);
  facing = Point(0, 0);
  velocity = Point(0, 0);
}

void Ship::fly() {
  cout << "Position: ";
  this->position.puts();
}

int main() {
  Ship *dog = new Ship(0, 0);
  dog->fly();
  return 0;
}
