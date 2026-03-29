#ifndef SCREENSHOT_GAME_H
#define SCREENSHOT_GAME_H

#include "glgame.h"

// A frozen, HUD-free game state used to produce store screenshots.
// Starts at 3840x1240 (set in glut.cpp), fills the view with normal and
// invincible asteroids while keeping the centre 860x380 safe-area clear.
class ScreenshotGame : public GLGame {
public:
  ScreenshotGame();
};

#endif
