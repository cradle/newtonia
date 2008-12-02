#ifndef TYPER_H
#define TYPER_H

class Typer {
public:
  Typer();
  
  void draw(float x, float y, int number, float size = 1);
  void draw(float x, float y, char *text, float size = 1);
  
private:
  float padding_proportion;
  
  void draw(float x, float y, char character, float size = 1);  
};

#endif