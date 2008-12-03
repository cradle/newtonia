#include "typer.h"

#include <string>

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/glut.h>
#endif

Typer::Typer() {
  padding_proportion = 0.35;
}

void Typer::draw(float x, float y, int number, float size) {
  bool negative = (number < 0);
  int i = 0;
  
  if(negative) {
    number *= -1;
  }
  
  do {
    draw(x-i*size-size*i*padding_proportion, y, char((number % 10)+48), size);
    i++;
    number = number / 10;
  } while(number != 0);
  
  if(negative) {
    draw(x-size*i-size*i*padding_proportion,y,'-',size);
  }
}

void Typer::draw(float x, float y, char * text, float size) {
  for(int i = 0; i < strlen(text); i++) {
    draw(x+i*10, y, text[i], size);
  }
}

void Typer::draw(float x, float y, char character, float size) {
  glPushMatrix();
  glColor3f(0,1,0);
  glTranslatef(x,y-size-padding_proportion*size,0);
  glScalef(size, size, 0);
  switch(character) {
    case '-':
      glBegin(GL_LINES);
      glVertex2i(0,1);
      glVertex2i(1,1);
      glEnd();
      break;
    case '0':
      glBegin(GL_LINE_LOOP);
      glVertex2i(0,2);
      glVertex2i(1,2);
      glVertex2i(1,0);
      glVertex2i(0,0);
      glEnd();
      break;
    case '1':
      glBegin(GL_LINES);
      glVertex2i(1,2);
      glVertex2i(1,0);
      glEnd();
      break;
    case '2':
      glBegin(GL_LINE_STRIP);
      glVertex2i(0,2);
      glVertex2i(1,2);
      glVertex2i(1,1);
      glVertex2i(0,1);
      glVertex2i(0,0);
      glVertex2i(1,0);
      glEnd();
      break;
    case '3':
      glBegin(GL_LINE_STRIP);
      glVertex2i(0,2);
      glVertex2i(1,2);
      glVertex2i(1,0);
      glVertex2i(0,0);
      glEnd();
      glBegin(GL_LINES);
      glVertex2i(0,1);
      glVertex2i(1,1);
      glEnd();
      break;
    case '4':
      glBegin(GL_LINE_STRIP);
      glVertex2i(0,2);
      glVertex2i(0,1);
      glVertex2i(1,1);
      glVertex2i(1,2);
      glEnd();
      glBegin(GL_LINES);
      glVertex2i(1,1);
      glVertex2i(1,0);
      glEnd();
      break;
    case '5':
      glBegin(GL_LINE_STRIP);
      glVertex2i(1,2);
      glVertex2i(0,2);
      glVertex2i(0,1);
      glVertex2i(1,1);
      glVertex2i(1,0);
      glVertex2i(0,0);
      glEnd();
      break;
    case '6':
      glBegin(GL_LINE_STRIP);
      glVertex2i(1,2);
      glVertex2i(0,2);
      glVertex2i(0,0);
      glVertex2i(1,0);
      glVertex2i(1,1);
      glVertex2i(0,1);
      glEnd();
      break;
    case '7':
      glBegin(GL_LINE_STRIP);
      glVertex2i(0,2);
      glVertex2i(1,2);
      glVertex2i(1,0);
      glEnd();
      break;
    case '8':
      glBegin(GL_LINE_LOOP);
      glVertex2i(1,2);
      glVertex2i(0,2);
      glVertex2i(0,0);
      glVertex2i(1,0);
      glEnd();
      glBegin(GL_LINES);
      glVertex2i(0,1);
      glVertex2i(1,1);
      glEnd();
      break;
    case '9':
      glBegin(GL_LINE_STRIP);
      glVertex2i(0,0);
      glVertex2i(1,0);
      glVertex2i(1,2);
      glVertex2i(0,2);
      glVertex2i(0,1);
      glVertex2i(1,1);
      glEnd();
      break;
        
  }
  glPopMatrix();
}