#ifndef TYPER_H
#define TYPER_H

#include "glship.h"
#include "mesh.h"
#include <string>
class GLShip;

class Typer {
public:
  // Selection cursor for a draw_centered row: "> ITEM <" when selected,
  // "  ITEM  " when not. Both forms pad symmetrically, so the item's own
  // glyphs sit on the centre line either way — a bare "> " prefix shifted
  // every selected row right by half a cursor's width.
  static std::string cursored(const std::string &text, bool selected);
  static void draw_lefted(float x, float y, int number, float size = 1, int time = 0);
  static void draw(float x, float y, int number, float size = 1, int time = 0);
  static void draw(float x, float y, const char *text, float size = 1, int time = 0);
  static void draw_centered(float x, float y, int number, float size = 1, int time = 0);
  static void draw_centered(float x, float y, const char *text, float size = 1, int time = 0);
  static void draw_lives(float x, float y, const GLShip *ship, float size = 1, int time = 0);
  static void draw(float x, float y, char character, float size = 1, int time = 0);
  static void draw_button(float x, float y, char c, float size = 1);
  static void resize(int width, int height);
  static void cleanup();
  // Batch support: read-only access to a glyph's retained CPU builder and
  // the current text colour, so many labels can be baked into ONE mesh
  // (AsteroidDrawer's dead-asteroid score labels) instead of one Typer draw
  // + two viewport swaps each. NULL for glyphs without a prebuilt builder.
  static const MeshBuilder *glyph_builder(unsigned char c);
  static const float *text_colour();
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
  static void init_meshes();
  static Mesh* char_meshes[256];
  static bool meshes_initialized;
};

#endif
