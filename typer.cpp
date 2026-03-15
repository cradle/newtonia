#include "typer.h"

#include <cstring>
#include <math.h>
#include "glship.h"
#include "gl_compat.h"

float Typer::colour[] = {0.0f,1.0f,0.0f};
GLuint Typer::char_lists[256] = {};
bool Typer::lists_initialized = false;
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
  glLineWidth(1.1f * scale);
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

// Geometry constants shared between init_lists() and the animated-char fallback.
// These match the local variables that were previously declared inside draw(char).
static const float TQ  = 0.25f;        // quarter_size
static const float TH  = 2.0f;         // height
static const float TMU = TH - TQ;      // mid_upper_height  = 1.75
static const float TM  = TH * 0.6f;    // mid_height        = 1.2
static const float TML = TM - TQ;      // mid_lower_height  = 0.95
static const float TW  = 1.0f;         // width
static const float TMW = TW * 0.67f;   // mid_width         = 0.67
static const float TC  = TW / 2.0f;    // center            = 0.5

// Compile the vertex geometry for every supported character into GL display
// lists so that draw(char) becomes pre_draw + glCallList + post_draw instead
// of many individual glBegin/glEnd pairs.
// The animated character (case '©') is excluded; its list entry stays 0.
void Typer::init_lists() {
  lists_initialized = true;

  // Helper: allocate one list, start compiling it, record its id.
  auto begin = [](unsigned char c) {
    GLuint id = glGenLists(1);
    char_lists[c] = id;
    glNewList(id, GL_COMPILE);
  };

  begin('-');
  glBegin(GL_LINES);
  glVertex2f(0,TM); glVertex2f(TW,TM);
  glEnd();
  glEndList();

  begin('.');
  glPointSize(2.0f);
  glBegin(GL_POINTS);
  glVertex2f(TW*0.5f, TH*0.125f);
  glEnd();
  glEndList();

  begin('+');
  glBegin(GL_LINES);
  glVertex2f(0,TM);  glVertex2f(TW,TM);
  glVertex2f(TW*0.5f,1.75f); glVertex2f(TW*0.5f,0.25f);
  glEnd();
  glEndList();

  begin('0');
  glBegin(GL_LINE_LOOP);
  glVertex2f(0,TH); glVertex2f(TW,TH); glVertex2f(TW,0); glVertex2f(0,0);
  glEnd();
  glBegin(GL_LINES);
  glVertex2f(TW,TH); glVertex2f(0,0);
  glEnd();
  glEndList();

  begin('1');
  glBegin(GL_LINES);
  glVertex2f(TMW,TH); glVertex2f(TMW,0);
  glEnd();
  glEndList();

  begin('2');
  glBegin(GL_LINE_STRIP);
  glVertex2f(0,TH); glVertex2f(TW,TH); glVertex2f(TW,TM);
  glVertex2f(0,TM); glVertex2f(0,0);   glVertex2f(TW,0);
  glEnd();
  glEndList();

  begin('3');
  glBegin(GL_LINE_STRIP);
  glVertex2f(0,TH); glVertex2f(TW,TH); glVertex2f(TW,0); glVertex2f(0,0);
  glEnd();
  glBegin(GL_LINES);
  glVertex2f(0,TM); glVertex2f(TW,TM);
  glEnd();
  glEndList();

  begin('4');
  glBegin(GL_LINE_STRIP);
  glVertex2f(0,TH); glVertex2f(0,TM); glVertex2f(TW,TM); glVertex2f(TW,TH);
  glEnd();
  glBegin(GL_LINES);
  glVertex2f(TW,TM); glVertex2f(TW,0);
  glEnd();
  glEndList();

  begin('5');
  glBegin(GL_LINE_STRIP);
  glVertex2f(TW,TH); glVertex2f(0,TH); glVertex2f(0,TM); glVertex2f(TW,TM);
  glVertex2f(TW,0);  glVertex2f(0,0);
  glEnd();
  glEndList();

  begin('6');
  glBegin(GL_LINE_STRIP);
  glVertex2f(TW,TH); glVertex2f(0,TH); glVertex2f(0,0);  glVertex2f(TW,0);
  glVertex2f(TW,TM); glVertex2f(0,TM);
  glEnd();
  glEndList();

  begin('7');
  glBegin(GL_LINE_STRIP);
  glVertex2f(0,TH); glVertex2f(TW,TH); glVertex2f(TW,0);
  glEnd();
  glEndList();

  begin('8');
  glBegin(GL_LINE_LOOP);
  glVertex2f(TW,TH); glVertex2f(0,TH); glVertex2f(0,0); glVertex2f(TW,0);
  glEnd();
  glBegin(GL_LINES);
  glVertex2f(0,TM); glVertex2f(TW,TM);
  glEnd();
  glEndList();

  begin('9');
  glBegin(GL_LINE_STRIP);
  glVertex2f(0,0);   glVertex2f(TW,0);  glVertex2f(TW,TH);
  glVertex2f(0,TH);  glVertex2f(0,TM);  glVertex2f(TW,TM);
  glEnd();
  glEndList();

  // Letters — upper and lower case share the same display list.
  char_lists['A'] = glGenLists(1);
  char_lists['a'] = char_lists['A'];
  glNewList(char_lists['A'], GL_COMPILE);
  glBegin(GL_LINE_STRIP);
  glVertex2f(0,0); glVertex2f(0,TH); glVertex2f(TW,TH); glVertex2f(TW,0);
  glEnd();
  glBegin(GL_LINES);
  glVertex2f(0,TM); glVertex2f(TW,TM);
  glEnd();
  glEndList();

  char_lists['B'] = glGenLists(1); char_lists['b'] = char_lists['B'];
  glNewList(char_lists['B'], GL_COMPILE);
  glBegin(GL_LINE_STRIP);
  glVertex2f(0,0); glVertex2f(0,TH); glVertex2f(TMW,TH); glVertex2f(TW,TMU);
  glVertex2f(TW,TM); glVertex2f(0,TM);
  glEnd();
  glBegin(GL_LINE_STRIP);
  glVertex2f(TMW,TM); glVertex2f(TW,TML); glVertex2f(TW,0); glVertex2f(0,0);
  glEnd();
  glEndList();

  char_lists['C'] = glGenLists(1); char_lists['c'] = char_lists['C'];
  glNewList(char_lists['C'], GL_COMPILE);
  glBegin(GL_LINE_STRIP);
  glVertex2f(TW,0); glVertex2f(0,0); glVertex2f(0,TH); glVertex2f(TW,TH);
  glEnd();
  glEndList();

  char_lists['D'] = glGenLists(1); char_lists['d'] = char_lists['D'];
  glNewList(char_lists['D'], GL_COMPILE);
  glBegin(GL_LINE_LOOP);
  glVertex2f(0,0); glVertex2f(0,TH); glVertex2f(TMW,TH); glVertex2f(TW,TMU);
  glVertex2f(TW,TM); glVertex2f(TW,0);
  glEnd();
  glEndList();

  char_lists['E'] = glGenLists(1); char_lists['e'] = char_lists['E'];
  glNewList(char_lists['E'], GL_COMPILE);
  glBegin(GL_LINE_STRIP);
  glVertex2f(TW,TH); glVertex2f(0,TH); glVertex2f(0,0); glVertex2f(TW,0);
  glEnd();
  glBegin(GL_LINES);
  glVertex2f(0,TM); glVertex2f(TMW,TM);
  glEnd();
  glEndList();

  char_lists['F'] = glGenLists(1); char_lists['f'] = char_lists['F'];
  glNewList(char_lists['F'], GL_COMPILE);
  glBegin(GL_LINE_STRIP);
  glVertex2f(TW,TH); glVertex2f(0,TH); glVertex2f(0,0);
  glEnd();
  glBegin(GL_LINES);
  glVertex2f(0,TM); glVertex2f(TW,TM);
  glEnd();
  glEndList();

  char_lists['G'] = glGenLists(1); char_lists['g'] = char_lists['G'];
  glNewList(char_lists['G'], GL_COMPILE);
  glBegin(GL_LINE_STRIP);
  glVertex2f(TW,TH); glVertex2f(0,TH); glVertex2f(0,0); glVertex2f(TW,0);
  glVertex2f(TW,TM);
  glEnd();
  glEndList();

  char_lists['H'] = glGenLists(1); char_lists['h'] = char_lists['H'];
  glNewList(char_lists['H'], GL_COMPILE);
  glBegin(GL_LINES);
  glVertex2f(TW,TM); glVertex2f(0,TM);
  glVertex2f(0,0);   glVertex2f(0,TH);
  glVertex2f(TW,0);  glVertex2f(TW,TH);
  glEnd();
  glEndList();

  char_lists['I'] = glGenLists(1); char_lists['i'] = char_lists['I'];
  glNewList(char_lists['I'], GL_COMPILE);
  glBegin(GL_LINES);
  glVertex2f(TC,TH); glVertex2f(TC,0);
  glEnd();
  glEndList();

  char_lists['J'] = glGenLists(1); char_lists['j'] = char_lists['J'];
  glNewList(char_lists['J'], GL_COMPILE);
  glBegin(GL_LINE_STRIP);
  glVertex2f(TW,TH); glVertex2f(TW,0); glVertex2f(0,0); glVertex2f(0,TM);
  glEnd();
  glEndList();

  char_lists['K'] = glGenLists(1); char_lists['k'] = char_lists['K'];
  glNewList(char_lists['K'], GL_COMPILE);
  glBegin(GL_LINES);
  glVertex2f(0,0);  glVertex2f(0,TH);
  glVertex2f(0,TM); glVertex2f(TW,TM);
  glVertex2f(TW,TM);glVertex2f(TW,0);
  glVertex2f(TMW,TM);glVertex2f(TW,TH);
  glEnd();
  glEndList();

  char_lists['L'] = glGenLists(1); char_lists['l'] = char_lists['L'];
  glNewList(char_lists['L'], GL_COMPILE);
  glBegin(GL_LINE_STRIP);
  glVertex2f(0,TH); glVertex2f(0,0); glVertex2f(TW,0);
  glEnd();
  glEndList();

  char_lists['M'] = glGenLists(1); char_lists['m'] = char_lists['M'];
  glNewList(char_lists['M'], GL_COMPILE);
  glBegin(GL_LINE_STRIP);
  glVertex2f(0,0); glVertex2f(0,TH); glVertex2f(TW,TH); glVertex2f(TW,0);
  glEnd();
  glBegin(GL_LINES);
  glVertex2f(TC,TH); glVertex2f(TC,TM);
  glEnd();
  glEndList();

  char_lists['N'] = glGenLists(1); char_lists['n'] = char_lists['N'];
  glNewList(char_lists['N'], GL_COMPILE);
  glBegin(GL_LINES);
  glVertex2f(0,0);   glVertex2f(0,TH);
  glVertex2f(0,TH);  glVertex2f(TW,TM);
  glVertex2f(TW,0);  glVertex2f(TW,TH);
  glEnd();
  glEndList();

  char_lists['O'] = glGenLists(1); char_lists['o'] = char_lists['O'];
  glNewList(char_lists['O'], GL_COMPILE);
  glBegin(GL_LINE_LOOP);
  glVertex2f(0,0); glVertex2f(0,TH); glVertex2f(TW,TH); glVertex2f(TW,0);
  glEnd();
  glEndList();

  char_lists['P'] = glGenLists(1); char_lists['p'] = char_lists['P'];
  glNewList(char_lists['P'], GL_COMPILE);
  glBegin(GL_LINE_STRIP);
  glVertex2f(0,0); glVertex2f(0,TH); glVertex2f(TW,TH); glVertex2f(TW,TM);
  glVertex2f(0,TM);
  glEnd();
  glEndList();

  char_lists['Q'] = glGenLists(1); char_lists['q'] = char_lists['Q'];
  glNewList(char_lists['Q'], GL_COMPILE);
  glBegin(GL_LINE_LOOP);
  glVertex2f(0,0); glVertex2f(0,TH); glVertex2f(TW,TH); glVertex2f(TW,TML);
  glVertex2f(TC,0);
  glEnd();
  glBegin(GL_LINES);
  glVertex2f(TC,TML); glVertex2f(TW,0);
  glEnd();
  glEndList();

  char_lists['R'] = glGenLists(1); char_lists['r'] = char_lists['R'];
  glNewList(char_lists['R'], GL_COMPILE);
  glBegin(GL_LINE_STRIP);
  glVertex2f(0,0); glVertex2f(0,TH); glVertex2f(TW,TH); glVertex2f(TW,TM);
  glVertex2f(0,TM);
  glEnd();
  glBegin(GL_LINES);
  glVertex2f(TC,TM); glVertex2f(TW,0);
  glEnd();
  glEndList();

  char_lists['S'] = glGenLists(1); char_lists['s'] = char_lists['S'];
  glNewList(char_lists['S'], GL_COMPILE);
  glBegin(GL_LINE_STRIP);
  glVertex2f(TW,TH); glVertex2f(0,TH); glVertex2f(0,TM); glVertex2f(TW,TM);
  glVertex2f(TW,0);  glVertex2f(0,0);
  glEnd();
  glEndList();

  char_lists['T'] = glGenLists(1); char_lists['t'] = char_lists['T'];
  glNewList(char_lists['T'], GL_COMPILE);
  glBegin(GL_LINES);
  glVertex2f(TC,TH); glVertex2f(TC,0);
  glVertex2f(0,TH);  glVertex2f(TW,TH);
  glEnd();
  glEndList();

  char_lists['U'] = glGenLists(1); char_lists['u'] = char_lists['U'];
  glNewList(char_lists['U'], GL_COMPILE);
  glBegin(GL_LINE_STRIP);
  glVertex2f(TW,TH); glVertex2f(TW,0); glVertex2f(0,0); glVertex2f(0,TH);
  glEnd();
  glEndList();

  char_lists['V'] = glGenLists(1); char_lists['v'] = char_lists['V'];
  glNewList(char_lists['V'], GL_COMPILE);
  glBegin(GL_LINE_STRIP);
  glVertex2f(TW,TH); glVertex2f(TW,TML); glVertex2f(TC,0); glVertex2f(0,0);
  glVertex2f(0,TH);
  glEnd();
  glEndList();

  char_lists['W'] = glGenLists(1); char_lists['w'] = char_lists['W'];
  glNewList(char_lists['W'], GL_COMPILE);
  glBegin(GL_LINE_STRIP);
  glVertex2f(0,TH); glVertex2f(0,0); glVertex2f(TW,0); glVertex2f(TW,TH);
  glEnd();
  glBegin(GL_LINES);
  glVertex2f(TC,TM); glVertex2f(TC,0);
  glEnd();
  glEndList();

  char_lists['X'] = glGenLists(1); char_lists['x'] = char_lists['X'];
  glNewList(char_lists['X'], GL_COMPILE);
  glBegin(GL_LINES);
  glVertex2f(TW,TH); glVertex2f(0,0);
  glVertex2f(0,TH);  glVertex2f(TW,0);
  glEnd();
  glEndList();

  char_lists['Y'] = glGenLists(1); char_lists['y'] = char_lists['Y'];
  glNewList(char_lists['Y'], GL_COMPILE);
  glBegin(GL_LINE_STRIP);
  glVertex2f(0,TH); glVertex2f(0,TM); glVertex2f(TW,TM); glVertex2f(TW,TH);
  glEnd();
  glBegin(GL_LINES);
  glVertex2f(TC,TM); glVertex2f(TC,0);
  glEnd();
  glEndList();

  char_lists['Z'] = glGenLists(1); char_lists['z'] = char_lists['Z'];
  glNewList(char_lists['Z'], GL_COMPILE);
  glBegin(GL_LINE_STRIP);
  glVertex2f(0,TH); glVertex2f(TW,TH); glVertex2f(0,0); glVertex2f(TW,0);
  glEnd();
  glEndList();

  begin('>');
  glBegin(GL_LINE_STRIP);
  glVertex2f(0,TH*0.9f); glVertex2f(TW,TH/2.0f); glVertex2f(0,TH*0.1f);
  glEnd();
  glEndList();

  begin('<');
  glBegin(GL_LINE_STRIP);
  glVertex2f(TW,TH*0.9f); glVertex2f(0,TH/2.0f); glVertex2f(TW,TH*0.1f);
  glEnd();
  glEndList();

  begin('/');
  glBegin(GL_LINE_STRIP);
  glVertex2f(TW,TH); glVertex2f(0,0);
  glEnd();
  glEndList();

  begin(',');
  glBegin(GL_LINE_STRIP);
  glVertex2f(TW/2.0f,TH/3.0f); glVertex2f(0,0);
  glEnd();
  glEndList();
}

void Typer::draw(float x, float y, char character, float size, int time) {
  if (!lists_initialized) init_lists();

  // Fast path: geometry is pre-compiled on the GPU.
  if (char_lists[(unsigned char)character] != 0) {
    pre_draw(x, y, size);
    glCallList(char_lists[(unsigned char)character]);
    post_draw();
    return;
  }

  // Fallback for characters with time-dependent geometry (the animated char).
  float segment_size = 360.0f / 7;
  pre_draw(x, y, size);
  switch(character) {
    case '\xa9': {
      float d;
      glBegin(GL_LINE_STRIP);
      glVertex2f(TW*TQ*3, TML);
      glVertex2f(TW*TQ,   TML);
      glVertex2f(TW*TQ,   TMU);
      glVertex2f(TW*TQ*3, TMU);
      glEnd();
      glTranslatef(0.5f, TM, 0.0f);
      glScalef(1.0f, 1.2f, 1.0f);
      glRotated(time / -16.0, 0.0f, 0.0f, 1.0f);
      glBegin(GL_LINE_LOOP);
      for (float i = 0.0f; i < 360.0f; i += segment_size) {
        d = i * (float)M_PI / 180.0f;
        glVertex2f(cosf(d), sinf(d));
      }
      glEnd();
      break;
    }
  }
  post_draw();
}
