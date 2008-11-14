#ifndef GAME_H
#define GAME_H

#include "glship.h"

class Game {
public:
  Game() {};
  Game(float width, float height);

  void init(int argc, char** argv);
  void run();
  
  void tick(void);
  void resize(int width, int height);
  void draw(void);
  void keyboard (unsigned char key, int x, int y);
  void keyboard_up (unsigned char key, int x, int y);
  
private:
  int window_width, window_height;
  int last_tick;
  GLShip player1;
  GLShip player2;
};

#endif