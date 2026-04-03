#include "typer.h"

#include <cstring>
#include <math.h>
#include <set>
#include "glship.h"
#include "gl_compat.h"

float Typer::colour[] = {0.0f,1.0f,0.0f};
Mesh* Typer::char_meshes[256] = {};
bool Typer::meshes_initialized = false;
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

// Geometry constants shared between init_meshes() and the animated-char fallback.
// These match the local variables that were previously declared inside draw(char).
static const float TQ  = 0.25f;        // quarter_size
static const float TH  = 2.0f;         // height
static const float TMU = TH - TQ;      // mid_upper_height  = 1.75
static const float TM  = TH * 0.6f;    // mid_height        = 1.2
static const float TML = TM - TQ;      // mid_lower_height  = 0.95
static const float TW  = 1.0f;         // width
static const float TMW = TW * 0.67f;   // mid_width         = 0.67
static const float TC  = TW / 2.0f;    // center            = 0.5

// Upload the vertex geometry for every supported character into a GPU Mesh so
// that draw(char) becomes pre_draw + draw_tinted + post_draw instead of many
// individual glBegin/glEnd pairs.
// The animated character (case '©') is excluded; its mesh pointer stays null.
void Typer::init_meshes() {
  meshes_initialized = true;

  // Helper: build a mesh for character c from a MeshBuilder.
  auto upload = [](unsigned char c, MeshBuilder& mb) {
    char_meshes[c] = new Mesh();
    char_meshes[c]->upload(mb);
  };

  { MeshBuilder mb;
    mb.begin(GL_LINES); mb.color(1,1,1);
    mb.vertex(0,TM); mb.vertex(TW,TM);
    mb.end(); upload('-', mb); }

  { MeshBuilder mb; mb.begin(GL_POINTS); mb.color(1,1,1);
    mb.vertex(TW*0.5f, TH*0.125f);
    mb.end(); upload('.', mb); }

  { MeshBuilder mb; mb.begin(GL_LINES); mb.color(1,1,1);
    mb.vertex(0,TM); mb.vertex(TW,TM);
    mb.vertex(TW*0.5f,1.75f); mb.vertex(TW*0.5f,0.25f);
    mb.end(); upload('+', mb); }

  { MeshBuilder mb; mb.color(1,1,1);
    mb.begin(GL_LINE_LOOP); mb.vertex(0,TH); mb.vertex(TW,TH); mb.vertex(TW,0); mb.vertex(0,0); mb.end();
    mb.begin(GL_LINES);     mb.vertex(TW,TH); mb.vertex(0,0); mb.end();
    upload('0', mb); }

  { MeshBuilder mb; mb.begin(GL_LINES); mb.color(1,1,1);
    mb.vertex(TMW,TH); mb.vertex(TMW,0);
    mb.end(); upload('1', mb); }

  { MeshBuilder mb; mb.begin(GL_LINE_STRIP); mb.color(1,1,1);
    mb.vertex(0,TH); mb.vertex(TW,TH); mb.vertex(TW,TM); mb.vertex(0,TM); mb.vertex(0,0); mb.vertex(TW,0);
    mb.end(); upload('2', mb); }

  { MeshBuilder mb; mb.color(1,1,1);
    mb.begin(GL_LINE_STRIP); mb.vertex(0,TH); mb.vertex(TW,TH); mb.vertex(TW,0); mb.vertex(0,0); mb.end();
    mb.begin(GL_LINES);      mb.vertex(0,TM); mb.vertex(TW,TM); mb.end();
    upload('3', mb); }

  { MeshBuilder mb; mb.color(1,1,1);
    mb.begin(GL_LINE_STRIP); mb.vertex(0,TH); mb.vertex(0,TM); mb.vertex(TW,TM); mb.vertex(TW,TH); mb.end();
    mb.begin(GL_LINES);      mb.vertex(TW,TM); mb.vertex(TW,0); mb.end();
    upload('4', mb); }

  { MeshBuilder mb; mb.begin(GL_LINE_STRIP); mb.color(1,1,1);
    mb.vertex(TW,TH); mb.vertex(0,TH); mb.vertex(0,TM); mb.vertex(TW,TM); mb.vertex(TW,0); mb.vertex(0,0);
    mb.end(); upload('5', mb); }

  { MeshBuilder mb; mb.begin(GL_LINE_STRIP); mb.color(1,1,1);
    mb.vertex(TW,TH); mb.vertex(0,TH); mb.vertex(0,0); mb.vertex(TW,0); mb.vertex(TW,TM); mb.vertex(0,TM);
    mb.end(); upload('6', mb); }

  { MeshBuilder mb; mb.begin(GL_LINE_STRIP); mb.color(1,1,1);
    mb.vertex(0,TH); mb.vertex(TW,TH); mb.vertex(TW,0);
    mb.end(); upload('7', mb); }

  { MeshBuilder mb; mb.color(1,1,1);
    mb.begin(GL_LINE_LOOP); mb.vertex(TW,TH); mb.vertex(0,TH); mb.vertex(0,0); mb.vertex(TW,0); mb.end();
    mb.begin(GL_LINES);     mb.vertex(0,TM); mb.vertex(TW,TM); mb.end();
    upload('8', mb); }

  { MeshBuilder mb; mb.begin(GL_LINE_STRIP); mb.color(1,1,1);
    mb.vertex(0,0); mb.vertex(TW,0); mb.vertex(TW,TH); mb.vertex(0,TH); mb.vertex(0,TM); mb.vertex(TW,TM);
    mb.end(); upload('9', mb); }

  // Letters — upper and lower case share the same Mesh pointer.
  auto upload_ab = [&](unsigned char upper, unsigned char lower, MeshBuilder& mb) {
    Mesh* m = new Mesh(); m->upload(mb);
    char_meshes[upper] = char_meshes[lower] = m;
  };

  { MeshBuilder mb; mb.color(1,1,1);
    mb.begin(GL_LINE_STRIP); mb.vertex(0,0); mb.vertex(0,TH); mb.vertex(TW,TH); mb.vertex(TW,0); mb.end();
    mb.begin(GL_LINES);      mb.vertex(0,TM); mb.vertex(TW,TM); mb.end();
    upload_ab('A','a',mb); }

  { MeshBuilder mb; mb.color(1,1,1);
    mb.begin(GL_LINE_STRIP); mb.vertex(0,0); mb.vertex(0,TH); mb.vertex(TMW,TH); mb.vertex(TW,TMU); mb.vertex(TW,TM); mb.vertex(0,TM); mb.end();
    mb.begin(GL_LINE_STRIP); mb.vertex(TMW,TM); mb.vertex(TW,TML); mb.vertex(TW,0); mb.vertex(0,0); mb.end();
    upload_ab('B','b',mb); }

  { MeshBuilder mb; mb.begin(GL_LINE_STRIP); mb.color(1,1,1);
    mb.vertex(TW,0); mb.vertex(0,0); mb.vertex(0,TH); mb.vertex(TW,TH);
    mb.end(); upload_ab('C','c',mb); }

  { MeshBuilder mb; mb.begin(GL_LINE_LOOP); mb.color(1,1,1);
    mb.vertex(0,0); mb.vertex(0,TH); mb.vertex(TMW,TH); mb.vertex(TW,TMU); mb.vertex(TW,TM); mb.vertex(TW,0);
    mb.end(); upload_ab('D','d',mb); }

  { MeshBuilder mb; mb.color(1,1,1);
    mb.begin(GL_LINE_STRIP); mb.vertex(TW,TH); mb.vertex(0,TH); mb.vertex(0,0); mb.vertex(TW,0); mb.end();
    mb.begin(GL_LINES);      mb.vertex(0,TM); mb.vertex(TMW,TM); mb.end();
    upload_ab('E','e',mb); }

  { MeshBuilder mb; mb.color(1,1,1);
    mb.begin(GL_LINE_STRIP); mb.vertex(TW,TH); mb.vertex(0,TH); mb.vertex(0,0); mb.end();
    mb.begin(GL_LINES);      mb.vertex(0,TM); mb.vertex(TW,TM); mb.end();
    upload_ab('F','f',mb); }

  { MeshBuilder mb; mb.begin(GL_LINE_STRIP); mb.color(1,1,1);
    mb.vertex(TW,TH); mb.vertex(0,TH); mb.vertex(0,0); mb.vertex(TW,0); mb.vertex(TW,TM);
    mb.end(); upload_ab('G','g',mb); }

  { MeshBuilder mb; mb.begin(GL_LINES); mb.color(1,1,1);
    mb.vertex(TW,TM); mb.vertex(0,TM);
    mb.vertex(0,0);   mb.vertex(0,TH);
    mb.vertex(TW,0);  mb.vertex(TW,TH);
    mb.end(); upload_ab('H','h',mb); }

  { MeshBuilder mb; mb.begin(GL_LINES); mb.color(1,1,1);
    mb.vertex(TC,TH); mb.vertex(TC,0);
    mb.end(); upload_ab('I','i',mb); }

  { MeshBuilder mb; mb.begin(GL_LINE_STRIP); mb.color(1,1,1);
    mb.vertex(TW,TH); mb.vertex(TW,0); mb.vertex(0,0); mb.vertex(0,TM);
    mb.end(); upload_ab('J','j',mb); }

  { MeshBuilder mb; mb.begin(GL_LINES); mb.color(1,1,1);
    mb.vertex(0,0);   mb.vertex(0,TH);
    mb.vertex(0,TM);  mb.vertex(TW,TM);
    mb.vertex(TW,TM); mb.vertex(TW,0);
    mb.vertex(TMW,TM);mb.vertex(TW,TH);
    mb.end(); upload_ab('K','k',mb); }

  { MeshBuilder mb; mb.begin(GL_LINE_STRIP); mb.color(1,1,1);
    mb.vertex(0,TH); mb.vertex(0,0); mb.vertex(TW,0);
    mb.end(); upload_ab('L','l',mb); }

  { MeshBuilder mb; mb.color(1,1,1);
    mb.begin(GL_LINE_STRIP); mb.vertex(0,0); mb.vertex(0,TH); mb.vertex(TW,TH); mb.vertex(TW,0); mb.end();
    mb.begin(GL_LINES);      mb.vertex(TC,TH); mb.vertex(TC,TM); mb.end();
    upload_ab('M','m',mb); }

  { MeshBuilder mb; mb.begin(GL_LINES); mb.color(1,1,1);
    mb.vertex(0,0);  mb.vertex(0,TH);
    mb.vertex(0,TH); mb.vertex(TW,TM);
    mb.vertex(TW,0); mb.vertex(TW,TH);
    mb.end(); upload_ab('N','n',mb); }

  { MeshBuilder mb; mb.begin(GL_LINE_LOOP); mb.color(1,1,1);
    mb.vertex(0,0); mb.vertex(0,TH); mb.vertex(TW,TH); mb.vertex(TW,0);
    mb.end(); upload_ab('O','o',mb); }

  { MeshBuilder mb; mb.begin(GL_LINE_STRIP); mb.color(1,1,1);
    mb.vertex(0,0); mb.vertex(0,TH); mb.vertex(TW,TH); mb.vertex(TW,TM); mb.vertex(0,TM);
    mb.end(); upload_ab('P','p',mb); }

  { MeshBuilder mb; mb.color(1,1,1);
    mb.begin(GL_LINE_LOOP); mb.vertex(0,0); mb.vertex(0,TH); mb.vertex(TW,TH); mb.vertex(TW,TML); mb.vertex(TC,0); mb.end();
    mb.begin(GL_LINES);     mb.vertex(TC,TML); mb.vertex(TW,0); mb.end();
    upload_ab('Q','q',mb); }

  { MeshBuilder mb; mb.color(1,1,1);
    mb.begin(GL_LINE_STRIP); mb.vertex(0,0); mb.vertex(0,TH); mb.vertex(TW,TH); mb.vertex(TW,TM); mb.vertex(0,TM); mb.end();
    mb.begin(GL_LINES);      mb.vertex(TC,TM); mb.vertex(TW,0); mb.end();
    upload_ab('R','r',mb); }

  { MeshBuilder mb; mb.begin(GL_LINE_STRIP); mb.color(1,1,1);
    mb.vertex(TW,TH); mb.vertex(0,TH); mb.vertex(0,TM); mb.vertex(TW,TM); mb.vertex(TW,0); mb.vertex(0,0);
    mb.end(); upload_ab('S','s',mb); }

  { MeshBuilder mb; mb.begin(GL_LINES); mb.color(1,1,1);
    mb.vertex(TC,TH); mb.vertex(TC,0);
    mb.vertex(0,TH);  mb.vertex(TW,TH);
    mb.end(); upload_ab('T','t',mb); }

  { MeshBuilder mb; mb.begin(GL_LINE_STRIP); mb.color(1,1,1);
    mb.vertex(TW,TH); mb.vertex(TW,0); mb.vertex(0,0); mb.vertex(0,TH);
    mb.end(); upload_ab('U','u',mb); }

  { MeshBuilder mb; mb.begin(GL_LINE_STRIP); mb.color(1,1,1);
    mb.vertex(TW,TH); mb.vertex(TW,TML); mb.vertex(TC,0); mb.vertex(0,0); mb.vertex(0,TH);
    mb.end(); upload_ab('V','v',mb); }

  { MeshBuilder mb; mb.color(1,1,1);
    mb.begin(GL_LINE_STRIP); mb.vertex(0,TH); mb.vertex(0,0); mb.vertex(TW,0); mb.vertex(TW,TH); mb.end();
    mb.begin(GL_LINES);      mb.vertex(TC,TM); mb.vertex(TC,0); mb.end();
    upload_ab('W','w',mb); }

  { MeshBuilder mb; mb.begin(GL_LINES); mb.color(1,1,1);
    mb.vertex(TW,TH); mb.vertex(0,0);
    mb.vertex(0,TH);  mb.vertex(TW,0);
    mb.end(); upload_ab('X','x',mb); }

  { MeshBuilder mb; mb.color(1,1,1);
    mb.begin(GL_LINE_STRIP); mb.vertex(0,TH); mb.vertex(0,TM); mb.vertex(TW,TM); mb.vertex(TW,TH); mb.end();
    mb.begin(GL_LINES);      mb.vertex(TC,TM); mb.vertex(TC,0); mb.end();
    upload_ab('Y','y',mb); }

  { MeshBuilder mb; mb.begin(GL_LINE_STRIP); mb.color(1,1,1);
    mb.vertex(0,TH); mb.vertex(TW,TH); mb.vertex(0,0); mb.vertex(TW,0);
    mb.end(); upload_ab('Z','z',mb); }

  { MeshBuilder mb; mb.begin(GL_LINE_STRIP); mb.color(1,1,1);
    mb.vertex(0,TH*0.9f); mb.vertex(TW,TH/2.0f); mb.vertex(0,TH*0.1f);
    mb.end(); upload('>', mb); }

  { MeshBuilder mb; mb.begin(GL_LINE_STRIP); mb.color(1,1,1);
    mb.vertex(TW,TH*0.9f); mb.vertex(0,TH/2.0f); mb.vertex(TW,TH*0.1f);
    mb.end(); upload('<', mb); }

  { MeshBuilder mb; mb.begin(GL_LINE_STRIP); mb.color(1,1,1);
    mb.vertex(TW,TH); mb.vertex(0,0);
    mb.end(); upload('/', mb); }

  { MeshBuilder mb; mb.begin(GL_LINE_STRIP); mb.color(1,1,1);
    mb.vertex(TW/2.0f,TH/3.0f); mb.vertex(0,0);
    mb.end(); upload(',', mb); }
}

void Typer::cleanup() {
  if (!meshes_initialized) return;
  // Upper and lower case share the same Mesh pointer, so track freed pointers
  // to avoid double-deleting.
  std::set<Mesh*> freed;
  for (int i = 0; i < 256; i++) {
    if (char_meshes[i] && freed.find(char_meshes[i]) == freed.end()) {
      freed.insert(char_meshes[i]);
      delete char_meshes[i];
    }
    char_meshes[i] = nullptr;
  }
  meshes_initialized = false;
}

void Typer::draw(float x, float y, char character, float size, int time) {
  if (!meshes_initialized) init_meshes();

  // Fast path: geometry is pre-built on the GPU.
  Mesh* m = char_meshes[(unsigned char)character];
  if (m) {
    pre_draw(x, y, size);
    m->draw_tinted(colour[0], colour[1], colour[2], 1.0f);
    post_draw();
    return;
  }

  // Fallback for characters with time-dependent geometry (the animated char).
  float segment_size = 360.0f / 7;
  pre_draw(x, y, size);
  glColor3fv(colour);
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
