#include "ship.h"

class GLShip {
public:
  GLShip(int x, int y);
  void step(float delta);
  void resize(float screen_width, float screen_height);
  void input(unsigned char key, bool pressed = true);
  void draw();
  
private:
  Ship ship;
  float window_width, window_height;
};