#ifndef SPLASH_H
#define SPLASH_H

#include <SDL.h>
#include "state.h"

class Splash : public State {
public:
  Splash();
  virtual ~Splash() {}

  void draw();
  void keyboard(unsigned char key, int x, int y);
  void keyboard_up(unsigned char key, int x, int y);
  void controller(SDL_Event event);
  void tick(int delta);

private:
  static const float ORTHO_W;
  static const float ORTHO_H;
};

#endif
