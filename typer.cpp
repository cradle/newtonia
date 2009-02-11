#include "typer.h"

#include <string>
#include "glship.h"
#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/glut.h>
#endif

float Typer::colour[] = {0.0f,1.0f,0.0f};

void Typer::draw_centered(float x, float y, int number, float size) {
  int length = 1, temp = number/10;
  while(temp != 0) {
    temp /= 10;
    length++;
  }
  draw(x+length*size/2.0f, y, number, size);
}

void Typer::draw(float x, float y, int number, float size) {
  bool negative = (number < 0);
  int i = 0;

  if(negative) {
    number *= -1;
  }

  do {
    draw(x-i*size-size*i, y, char((number % 10)+48), size);
    i++;
    number = number / 10;
  } while(number != 0);

  if(negative) {
    draw(x-size*i-size*i,y,'-',size);
  }
}

void Typer::draw_lives(float x, float y, GLShip *ship, float size) {
  for(int i = 0; i < ship->ship->lives; i++) {
    draw_life(x-i*size-size*i, y, ship, size);
  }
}

void Typer::draw_centered(float x, float y, const char * text, float size) {
  draw(x-size*strlen(text), y, text, size);
}

void Typer::draw(float x, float y, const char * text, float size) {
  for(unsigned int i = 0; i < strlen(text); i++) {
    draw(x+i*size+size*i, y, text[i], size);
  }
}

void Typer::pre_draw(float x, float y, float size) {
  glPushMatrix();
  glLineWidth(1.3f);
  glColor3f(colour[0], colour[1], colour[2]);
  glTranslatef(x,y-2*size,0);
  glScalef(size, size, 0);
}

void Typer::post_draw() {
  glPopMatrix();
}

void Typer::draw_life(float x, float y, GLShip* ship, float size) {
  pre_draw(x,y,size);
  ship->draw_body();
  post_draw();
}

void Typer::draw(float x, float y, char character, float size) {
  float quarter_size = 0.25;
  float height = 2.0;
  float mid_upper_height = height - quarter_size;
  float mid_height = height * 0.6;
  float mid_lower_height = mid_height - quarter_size;
  float width = 1.0;
  float mid_width = width * 0.67;
  float center = width / 2.0;
  pre_draw(x,y,size);
  switch(character) {
    case '-':
      glBegin(GL_LINES);
      glVertex2f(0,mid_height);
      glVertex2f(width,mid_height);
      glEnd();
      break;
    case '0':
      glBegin(GL_LINE_LOOP);
      glVertex2f(0,height);
      glVertex2f(width,height);
      glVertex2f(width,0);
      glVertex2f(0,0);
      glEnd();
      break;
    case '1':
      glBegin(GL_LINES);
      glVertex2f(width,height);
      glVertex2f(width,0);
      glEnd();
      break;
    case '2':
      glBegin(GL_LINE_STRIP);
      glVertex2f(0,height);
      glVertex2f(width,height);
      glVertex2f(width,mid_height);
      glVertex2f(0,mid_height);
      glVertex2f(0,0);
      glVertex2f(width,0);
      glEnd();
      break;
    case '3':
      glBegin(GL_LINE_STRIP);
      glVertex2f(0,height);
      glVertex2f(width,height);
      glVertex2f(width,0);
      glVertex2f(0,0);
      glEnd();
      glBegin(GL_LINES);
      glVertex2f(0,mid_height);
      glVertex2f(width,mid_height);
      glEnd();
      break;
    case '4':
      glBegin(GL_LINE_STRIP);
      glVertex2f(0,height);
      glVertex2f(0,mid_height);
      glVertex2f(width,mid_height);
      glVertex2f(width,height);
      glEnd();
      glBegin(GL_LINES);
      glVertex2f(width,mid_height);
      glVertex2f(width,0);
      glEnd();
      break;
    case '5':
      glBegin(GL_LINE_STRIP);
      glVertex2f(width,height);
      glVertex2f(0,height);
      glVertex2f(0,mid_height);
      glVertex2f(width,mid_height);
      glVertex2f(width,0);
      glVertex2f(0,0);
      glEnd();
      break;
    case '6':
      glBegin(GL_LINE_STRIP);
      glVertex2f(width,height);
      glVertex2f(0,height);
      glVertex2f(0,0);
      glVertex2f(width,0);
      glVertex2f(width,mid_height);
      glVertex2f(0,mid_height);
      glEnd();
      break;
    case '7':
      glBegin(GL_LINE_STRIP);
      glVertex2f(0,height);
      glVertex2f(width,height);
      glVertex2f(width,0);
      glEnd();
      break;
    case '8':
      glBegin(GL_LINE_LOOP);
      glVertex2i(width,height);
      glVertex2i(0,height);
      glVertex2i(0,0);
      glVertex2i(width,0);
      glEnd();
      glBegin(GL_LINES);
      glVertex2i(0,mid_height);
      glVertex2i(width,mid_height);
      glEnd();
      break;
    case '9':
      glBegin(GL_LINE_STRIP);
      glVertex2i(0,0);
      glVertex2i(width,0);
      glVertex2i(width,height);
      glVertex2i(0,height);
      glVertex2i(0,mid_height);
      glVertex2i(width,mid_height);
      glEnd();
      break;
    case 'a':
    case 'A':
      glBegin(GL_LINE_STRIP);
      glVertex2i(0,0);
      glVertex2i(0,height);
      glVertex2i(width,height);
      glVertex2i(width,0);
      glEnd();
      glBegin(GL_LINES);
      glVertex2i(0,mid_height);
      glVertex2i(width,mid_height);
      glEnd();
      break;
    case 'b':
    case 'B':
      glBegin(GL_LINE_STRIP);
      glVertex2f(0.0,0.0);
      glVertex2f(0.0,height);
      glVertex2f(mid_width,height);
      glVertex2f(width,mid_upper_height);
      glVertex2f(width,mid_height);
      glVertex2f(0.0,mid_height);
      glEnd();
      glBegin(GL_LINE_STRIP);
      glVertex2f(mid_width,mid_height);
      glVertex2f(width,mid_lower_height);
      glVertex2f(width,0);
      glVertex2f(0.0,0.0);
      glEnd();
      break;
    case 'c':
    case 'C':
      glBegin(GL_LINE_STRIP);
      glVertex2f(width,0.0);
      glVertex2f(0.0,0.0);
      glVertex2f(0.0,height);
      glVertex2f(width,height);
      glEnd();
      break;
    case 'd':
    case 'D':
      glBegin(GL_LINE_LOOP);
      glVertex2f(0.0f,0.0f);
      glVertex2f(0.0f,height);
      glVertex2f(mid_width,height);
      glVertex2f(width,mid_upper_height);
      glVertex2f(width,mid_height);
      glVertex2f(width, 0.0f);
      glEnd();
      break;
    case 'e':
    case 'E':
      glBegin(GL_LINE_STRIP);
      glVertex2f(width,height);
      glVertex2f(0,height);
      glVertex2f(0,0);
      glVertex2f(width,0);
      glEnd();
      glBegin(GL_LINES);
      glVertex2f(0,mid_height);
      glVertex2f(width,mid_height);
      glEnd();
      break;
    case 'f':
    case 'F':
      glBegin(GL_LINE_STRIP);
      glVertex2f(width,height);
      glVertex2f(0,height);
      glVertex2f(0,0);
      glEnd();
      glBegin(GL_LINES);
      glVertex2f(0,mid_height);
      glVertex2f(width,mid_height);
      glEnd();
      break;
    case 'g':
    case 'G':
      glBegin(GL_LINE_STRIP);
      glVertex2f(width,height);
      glVertex2f(0,height);
      glVertex2f(0,0);
      glVertex2f(width,0);
      glVertex2f(width,mid_height);
      glEnd();
      break;
    case 'h':
    case 'H':
      glBegin(GL_LINES);
      glVertex2f(width,mid_height);
      glVertex2f(0,mid_height);
      glVertex2f(0,0);
      glVertex2f(0,height);
      glVertex2f(width,0);
      glVertex2f(width,height);
      glEnd();
      break;
    case 'i':
    case 'I':
      glBegin(GL_LINES);
      glVertex2f(center,height);
      glVertex2f(center,0);
      glEnd();
      break;
    case 'j':
    case 'J':
      glBegin(GL_LINE_STRIP);
      glVertex2f(width,height);
      glVertex2f(width,0);
      glVertex2f(0,0);
      glVertex2f(0,mid_height);
      glEnd();
      break;
    case 'k':
    case 'K':
      glBegin(GL_LINES);
      glVertex2f(0.0f, 0.0f);
      glVertex2f(0.0f, height);
      glVertex2f(0.0f , mid_height);
      glVertex2f(width, mid_height);
      glVertex2f(width, mid_height);
      glVertex2f(width, 0.0f);
      glVertex2f(mid_width, mid_height);
      glVertex2f(width, height);
      glEnd();
      break;
    case 'L':
    case 'l':
      glBegin(GL_LINE_STRIP);
      glVertex2f(0.0f, height);
      glVertex2f(0.0f, 0.0f);
      glVertex2f(width, 0.0f);
      glEnd();
      break;
    case 'M':
    case 'm':
      glBegin(GL_LINE_STRIP);
      glVertex2f(0.0f, 0.0f);
      glVertex2f(0.0f, height);
      glVertex2f(width, height);
      glVertex2f(width, 0.0f);
      glEnd();
      glBegin(GL_LINES);
      glVertex2f(center, height);
      glVertex2f(center, mid_height);
      glEnd();
      break;
    case 'n':
    case 'N':
      glBegin(GL_LINES);
      glVertex2f(0.0f, 0.0f);
      glVertex2f(0.0f, height);
      glVertex2f(0.0f, height);
      glVertex2f(width, mid_height);
      glVertex2f(width, 0.0f);
      glVertex2f(width, height);
      glEnd();
      break;
    case 'o':
    case 'O':
      glBegin(GL_LINE_LOOP);
      glVertex2f(0.0f, 0.0f);
      glVertex2f(0.0f, height);
      glVertex2f(width, height);
      glVertex2f(width, 0.0f);
      glEnd();
      break;
    case 'p':
    case 'P':
      glBegin(GL_LINE_STRIP);
      glVertex2f(0.0f, 0.0f);
      glVertex2f(0.0f, height);
      glVertex2f(width, height);
      glVertex2f(width, mid_height);
      glVertex2f(0.0f, mid_height);
      glEnd();
      break;
    case 'q':
    case 'Q':
      glBegin(GL_LINE_LOOP);
      glVertex2f(0.0f, 0.0f);
      glVertex2f(0.0f, height);
      glVertex2f(width, height);
      glVertex2f(width, mid_lower_height);
      glVertex2f(center, 0.0f);
      glEnd();
      glBegin(GL_LINES);
      glVertex2f(center, mid_lower_height);
      glVertex2f(width, 0.0f);
      glEnd();
      break;
    case 'r':
    case 'R':
      glBegin(GL_LINE_STRIP);
      glVertex2f(0.0f, 0.0f);
      glVertex2f(0.0f, height);
      glVertex2f(width, height);
      glVertex2f(width, mid_height);
      glVertex2f(0.0f, mid_height);
      glEnd();
      glBegin(GL_LINES);
      glVertex2f(center, mid_height);
      glVertex2f(width, 0.0f);
      glEnd();
      break;
    case 's':
    case 'S':
      glBegin(GL_LINE_STRIP);
      glVertex2f(width,height);
      glVertex2f(0.0f,height);
      glVertex2f(0.0f,mid_height);
      glVertex2f(width,mid_height);
      glVertex2f(width,0.0f);
      glVertex2f(0.0f,0.0f);
      glEnd();
      break;
    case 't':
    case 'T':
      glBegin(GL_LINES);
      glVertex2f(center,height);
      glVertex2f(center,0);
      glVertex2f(0.0f, height);
      glVertex2f(width, height);
      glEnd();
      break;
    case 'u':
    case 'U':
      glBegin(GL_LINE_STRIP);
      glVertex2f(width,height);
      glVertex2f(width,0.0f);
      glVertex2f(0.0f,0.0f);
      glVertex2f(0.0f,height);
      glEnd();
      break;
    case 'v':
    case 'V':
      glBegin(GL_LINE_STRIP);
      glVertex2f(width,height);
      glVertex2f(width,mid_lower_height);
      glVertex2f(center,0.0f);
      glVertex2f(0.0f, 0.0f);
      glVertex2f(0.0f,height);
      glEnd();
      break;
    case 'w':
    case 'W':
      glBegin(GL_LINE_STRIP);
      glVertex2f(0.0f, height);
      glVertex2f(0.0f, 0.0f);
      glVertex2f(width, 0.0f);
      glVertex2f(width, height);
      glEnd();
      glBegin(GL_LINES);
      glVertex2f(center, mid_height);
      glVertex2f(center, 0.0f);
      glEnd();
      break;
    case 'x':
    case 'X':
      glBegin(GL_LINES);
      glVertex2f(width, height);
      glVertex2f(0.0f, 0.0f);
      glVertex2f(0.0f, height);
      glVertex2f(width, 0.0f);
      glEnd();
      break;
    case 'y':
    case 'Y':
      glBegin(GL_LINE_STRIP);
      glVertex2f(0.0f, height);
      glVertex2f(0.0f, mid_height);
      glVertex2f(width, mid_height);
      glVertex2f(width, height);
      glEnd();
      glBegin(GL_LINES);
      glVertex2f(center, mid_height);
      glVertex2f(center, 0.0f);
      glEnd();
      break;
    case 'z':
    case 'Z':
      glBegin(GL_LINE_STRIP);
      glVertex2f(0.0f, height);
      glVertex2f(width, height);
      glVertex2f(0.0f, 0.0f);
      glVertex2f(width, 0.0f);
      glEnd();
      break;
  }
  post_draw();
}
