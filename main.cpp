#include <iostream>

using namespace std;

// TODO: Must remain normalised if facing (use polar?)
class Point {
  public:
    Point();
    Point(const Point& other);
    Point(float x, float y);
    char* to_s();
    void rotate(int degrees);
    Point operator+=(const Point other);
    Point operator*(float scalar);
  private:
    float x;
    float y;
};

ostream& operator << (ostream& os, const Point& p);

ostream& operator << (ostream& os, const Point& p)
{
  return os << p.to_s() << endl;
}

Point::Point() {
  // TODO: why do I need this constructor?
  x = 0;
  y = 0;
}

Point::Point(const Point& other) {
  x = other.x
  y = other.y
}

Point::Point(float x, float y) {
  this->x = x;
  this->y = y;
}

char* Point::to_s() {
  return "hai I is point";
  // return "(x,y): (" + x + "," + y + ")";
}

void Point::rotate(int degrees) {
  //TODO: implement rotation
}

Point Point::operator+=(const Point other) {
  x += other.x;
  y += other.y;
}

Point Point::operator*(float scalar) {
  return Point(x * scalar, y * scalar);
}

class Ship {
  public:
    Ship(float x, float y);
    // You must call inputs between every call to step
    void rotate_clockwise();
    void rotate_counterclockwise();
    void thrust();
    
    // Step moves the engine forward delta seconds, resets inputs
    void step(float delta);
    
    void puts();

  private:
    Point position;
    Point facing;
    Point acceleration;
    Point velocity;
};

Ship::Ship(float x, float y) {
  position = Point(x, y);
  facing = Point(1, 0);
  velocity = Point(0, 0);
  acceleration =  Point(0, 0);
}

void Ship::thrust() {
  acceleration = Point(facing);
}

void Ship::rotate_clockwise() {
  facing.rotate(1);
}

void Ship::rotate_counterclockwise() {
  facing.rotate(-1);
}

void Ship::puts() {
  cout << "Position: " <<  position;
}

void Ship::step(float delta) {
  velocity += acceleration * delta;
  position += velocity * delta;
  acceleration = Point();
}

int main() {
  Ship ship = Ship(0.0, 0.0);
  char input = 'h';
  //FIX: The ship is moving for some reason when this sim starts
  float delta = 0.1;
  //TODO: Play with function pointers in a 'hash' or similar
  while(input != 'q' && input != 'Q') {
    switch(input) {
      case 'h': {
        cout << "c: continue, t: thrust, r: right, l: left, s: step size, h: this message\n";
      }
      case 't': {
        ship.thrust();
        break;
      }
      case 'r': {
        ship.rotate_clockwise();
        break;
      }
      case 'l': {
        ship.rotate_counterclockwise();
        break;
      }
      case 'p': {
        ship.puts();
        break;
      }
      case 's': {
        cout << "step size=";
        cin >> delta;
        break;
      }
    }
    ship.step(delta);
    ship.puts();
    cout << "do: ";
    cin >> input;    
  }
  return 0;
}
