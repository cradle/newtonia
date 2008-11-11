#include <iostream>
#include <math.h>

using namespace std;

class Point {
  public:
    Point();
    Point(const Point& other);
    Point(float x, float y);
    char* to_s();
    void rotate(float radians);
    void operator+=(const Point other);
    Point operator*(float scalar);
    
  friend ostream& operator << (ostream& os, const Point& p);
  
  private:
    float x;
    float y;
};

ostream& operator << (ostream& os, const Point& p)
{
  return os << "(x,y): (" << p.x << "," << p.y << ")";
}

Point::Point() {
  // TODO: why do I need this constructor?
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

//TODO: Write tests
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
    
    // Step moves the engine forward delta seconds, zeroes forces
    void step(float delta);
    
    void puts();

  private:
    enum Rotation { 
      LEFT = -1, 
      NONE = 0, 
      RIGHT = 1 
    };

    // Linear
    Point position;
    Point acceleration;
    Point velocity;
    
    // Angular
    Point facing;
    Rotation rotation;

};

Ship::Ship(float x, float y) {
  position = Point(x, y);
  facing = Point(1, 0);
  velocity = Point(0, 0);
  acceleration =  Point(0, 0);
  rotation = NONE;
}

void Ship::thrust() {
  acceleration = Point(facing);
}

void Ship::rotate_clockwise() {
  rotation = LEFT;
}

void Ship::rotate_counterclockwise() {
  rotation = RIGHT;
}

void Ship::puts() {
  cout << "Facing: " << facing;
  cout << " Position: " << position;
  cout << " Velocity: " << velocity;
  cout << " Acceleration: " << acceleration;
  cout << endl;
}

void Ship::step(float delta) {
  // TODO: Move to force based system
  // acceleration = force / mass;
  // force = Point(0,0);
  
  // Step physics
  facing.rotate(rotation * delta);
  velocity += acceleration * delta;
  position += velocity * delta;
  
  // Reset forces
  rotation = NONE;
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
        break;
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
