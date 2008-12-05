#ifndef TYPER_H
#define TYPER_H

class Typer {
public:
  Typer();
  
  void draw(float x, float y, int number, float size = 1);
  void draw(float x, float y, char *text, float size = 1);
  void draw_lives(float x, float y, int lives, float size = 1);
  
private:
  float padding_proportion;
  
  void pre_draw(float x, float y, float size = 1);
  void post_draw();
  void draw_life(float x, float y, float size = 1);
  void draw(float x, float y, char character, float size = 1);  
};

#endif