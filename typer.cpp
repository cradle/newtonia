#include "typer.h"

#include <cstring>
#include <math.h>
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
const int Typer::original_window_width = 800;
int Typer::window_width = Typer::original_window_width;
float Typer::scaled_window_height = Typer::window_height;
const int Typer::original_window_height = 600;
int Typer::window_height = Typer::original_window_height;
float Typer::scaled_window_width = Typer::window_width;
float Typer::scale = 1.0f;
float Typer::window_x_scale = 1.0f;
float Typer::window_y_scale = 1.0f;
float Typer::aspect_ratio = window_width / window_height;
//}
 void Typer::draw_centered(float x, float y, int number, float size, int time) {
   int length = -1;
   int temp = number/10;
   while(temp != 0) {
     temp /= 10;
     length++;
   }
   draw(x+(length+0.5)*size, y, number, size, time);
}

void Typer::resize(int x, int y) {
  window_width = x;
  window_height = y;
  window_x_scale = (float)window_width / (float)original_window_width;
  window_y_scale = (float)window_height / (float)original_window_height;
  scale = window_x_scale;
  aspect_ratio = (float)window_x_scale / (float)window_y_scale;
  if(window_y_scale < window_x_scale) {
    scale = window_y_scale;
  }
  if(aspect_ratio > 1) {
    scaled_window_width = original_window_width * aspect_ratio;
    scaled_window_height = original_window_height;
  } else {
    scaled_window_width = original_window_width;
    scaled_window_height = original_window_height / aspect_ratio;
  }
  cout << "scale:" << scale << endl;
}

void Typer::draw_lefted(float x, float y, int number, float size, int time) {
  int length = -1;
  int temp = number/10;
  while(temp != 0) {
    temp /= 10;
    length++;
  }
  draw(x+length*size*2, y, number, size, time);
}

void Typer::draw(float x, float y, int number, float size, int time) {
  bool negative = (number < 0);
  int i = 0;

  if(negative) {
    number *= -1;
  }

  do {
    draw(x-i*size*2, y, char((number % 10)+48), size, time);
    i++;
    number = number / 10;
  } while(number != 0);

  if(negative) {
    draw(x-size*i-size*i,y,'-',size, time);
  }
}

void Typer::draw_lives(float x, float y, const GLShip *ship, float size, int time) {
  for(int i = 0; i < ship->ship->lives; i++) {
    draw_life(x-i*size*2, y, ship, size);
  }
}

void Typer::draw_centered(float x, float y, const char * text, float size, int time) {
  draw(x-size*(strlen(text)+0.5)+size, y, text, size, time);
}

void Typer::draw(float x, float y, const char * text, float size, int time) {
  for(unsigned int i = 0; i < strlen(text); i++) {
    draw(x+i*size*2, y, text[i], size, time);
  }
}

void Typer::pre_draw(float x, float y, float size) {
  glPushMatrix();
  glLineWidth(1.4f);
  glColor3fv(colour);
  glTranslatef(x*scale,(y*scale-2*size*scale),0);
  glScalef(size*scale, size*scale, 0);
}

void Typer::post_draw() {
  glPopMatrix();
}

void Typer::draw_life(float x, float y, const GLShip* ship, float size) {
  pre_draw(x,y,size);
  ship->draw_body();
  post_draw();
}

void Typer::draw(float x, float y, char character, float size, int time) {
  float quarter_size = 0.25;
  float height = 2.0;
  float mid_upper_height = height - quarter_size;
  float mid_height = height * 0.6;
  float mid_lower_height = mid_height - quarter_size;
  float width = 1.0;
  float mid_width = width * 0.67;
  float center = width / 2.0;
  int segment_count = 7;
  float segment_size = 360.0/segment_count, d;
  pre_draw(x,y,size);
  switch(character) {
    case '�':
      glBegin(GL_LINE_STRIP);
      glVertex2f(width*quarter_size*3,mid_lower_height);
      glVertex2f(width*quarter_size,mid_lower_height);
      glVertex2f(width*quarter_size,mid_upper_height);
      glVertex2f(width*quarter_size*3,mid_upper_height);
      glEnd();
      glTranslatef(0.5f,mid_height,0.0f);
      glScalef(1.0f,1.2f,1.0f);
      glRotated(time/-16.0, 0.0f, 0.0f, 1.0f);
      glBegin(GL_LINE_LOOP);
      for (float i = 0.0; i < 360.0; i+= segment_size) {
        d = i*M_PI/180;
        glVertex2f(cos(d),sin(d));
      }
      glEnd();
      break;
    case '-':
      glBegin(GL_LINES);
      glVertex2f(0,mid_height);
      glVertex2f(width,mid_height);
      glEnd();
      break;
    case '.':
      glPointSize(2.0f);
      glBegin(GL_POINTS);
      glVertex2f(width*0.5,height*0.125);
      glEnd();
      break;
    case '+':
      glBegin(GL_LINES);
      glVertex2f(0,mid_height);
      glVertex2f(width,mid_height);
      glVertex2f(width*0.5,1.75);
      glVertex2f(width*0.5,0.25);
      glEnd();
      break;
    case '0':
      glBegin(GL_LINE_LOOP);
      glVertex2f(0,height);
      glVertex2f(width,height);
      glVertex2f(width,0);
      glVertex2f(0,0);
      glEnd();
      glBegin(GL_LINES);
      glVertex2f(width,height);
      glVertex2f(0.0f, 0.0f);
      glEnd();
      break;
    case '1':
      glBegin(GL_LINES);
      glVertex2f(mid_width,height);
      glVertex2f(mid_width,0);
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
      glVertex2f(mid_width,mid_height);
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
    case '>':
      glBegin(GL_LINE_STRIP);
      glVertex2f(0.0f, height*0.9f);
      glVertex2f(width, height/2.0f);
      glVertex2f(0.0f, height*0.1f);
      glEnd();
      break;
    case '<':
      glBegin(GL_LINE_STRIP);
      glVertex2f(width, height*0.9f);
      glVertex2f(0.0f, height/2.0f);
      glVertex2f(width, height*0.1f);
      glEnd();
      break;
    case '/':
      glBegin(GL_LINE_STRIP);
      glVertex2f(width,height);
      glVertex2f(0.0f, 0.0f);
      glEnd();
      break;
    case ',':
      glBegin(GL_LINE_STRIP);
      glVertex2f(width/2.0f,height/3.0f);
      glVertex2f(0.0f, 0.0f);
      glEnd();
      break;
  }
  post_draw();
}
