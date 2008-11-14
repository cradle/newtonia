#include <iostream>
#include "ship.h"

using namespace std;

int main() {
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
        ship.rotate_left();
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