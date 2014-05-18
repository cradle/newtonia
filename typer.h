#ifndef TYPER_H
#define TYPER_H

#include "glship.h"
class GLShip;

class Typer {
public:
  static void draw_lefted(float x, float y, int number, float size = 1, int time = 0);
  static void draw(float x, float y, int number, float size = 1, int time = 0);
  static void draw(float x, float y, const char *text, float size = 1, int time = 0);
  static void draw_centered(float x, float y, int number, float size = 1, int time = 0);
  static void draw_centered(float x, float y, const char *text, float size = 1, int time = 0);
  static void draw_lives(float x, float y, const GLShip *ship, float size = 1, int time = 0);
  static void draw(float x, float y, char character, float size = 1, int time = 0);
  static void resize(int width, int height);
  static const int original_window_width, original_window_height;
  static float scaled_window_width, scaled_window_height;
  static int window_width, window_height;
  static float aspect_ratio;
  static float window_x_scale, window_y_scale, scale;

private:
  static float colour[3];
  static void pre_draw(float x, float y, float size = 1);
  static void post_draw();
  static void draw_life(float x, float y, const GLShip *ship, float size = 1);
};

#endif
