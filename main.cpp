#include <iostream>
#include <math.h>

using namespace std;

class Point {
  public:
    Point();
    Point(const Point& other);
    Point(float x, float y);
    
    void rotate(float radians);
    float direction();
    
    void operator+=(const Point other);
    Point operator*(float scalar);
    Point operator/(float scalar);
    
  friend ostream& operator << (ostream& os, const Point& p);
  
  //TODO: Make friend/private for view
    float x;
    float y;
};

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

//TODO: Write tests
Point Point::operator*(float scalar) {
  return Point(x * scalar, y * scalar);
}

Point Point::operator/(float scalar) {
  return Point(x / scalar, y / scalar);
}

class Ship {
  public:
    Ship(float x, float y);
    void rotate_left(bool on = true);
    void rotate_right(bool on = true);
    void thrust(bool on = true);
    // TODO: make 'friend' with some sort of VIEW
    bool thrusting;
    Point position;
    float heading();
    
    // Step moves the engine forward delta seconds, zeroes forces
    void step(float delta);
    
    void puts();

  private:
    enum Rotation { 
      LEFT = 1, 
      NONE = 0, 
      RIGHT = -1 
    };

    float mass;

    // Linear
    float thrust_force;
    Point velocity;
    
    // Angular
    float rotation_force;
    Point facing;
    Rotation rotation_direction;

};

Ship::Ship(float x, float y) {
  mass = 100.0;
  thrusting = false;
  thrust_force = 0.0005;
  position = Point(x, y);
  facing = Point(0, 1);
  velocity = Point(0, 0);
  rotation_force = 0.3;
  rotation_direction = NONE;
}

void Ship::thrust(bool on) {
  thrusting = on;
}

float Ship::heading() {
  //FIX: shouldn't have to calculate this each time
  facing.direction();
}

void Ship::rotate_left(bool on) {
  rotation_direction = on ? LEFT : NONE;
}

void Ship::rotate_right(bool on) {
  rotation_direction = on ? RIGHT : NONE;
}

void Ship::puts() {
  cout << "Facing: " << facing;
  cout << " Position: " << position;
  cout << " Velocity: " << velocity;
  cout << endl;
}

void Ship::step(float delta) {
  // TODO: Move to acceleration/force based rotation
  // Step physics
  facing.rotate(rotation_direction * rotation_force / mass  * delta );
  if(thrusting)
    velocity += ((facing * thrust_force) / mass) * delta;
  position += velocity * delta;
}

int test() {
  Ship ship = Ship(0.0, 0.0);
  char input = 'h';
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
        ship.rotate_right();
        break;
      }
      case 'l': {
        ship.rotate_right();
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
