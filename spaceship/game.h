#ifndef GAME_H
#define GAME_H

#include "ship.h"
#include <vector>

class Game {
public:
  Game() {};
  Game(float width, float height);
  
  void step(float delta);
  void resize(int width, int height);
  
  void press(unsigned char key);
  void release(unsigned char key);

private:
  int window_width, window_height;
  std::vector<Ship> ships;
};

#endif