#include "glstarfield.h"


#include "gl_compat.h"
#include "typer.h"

#include <math.h>

const int GLStarfield::NUM_REAR_LAYERS = 10;
const int GLStarfield::NUM_FRONT_LAYERS = 5;
const float GLStarfield::STAR_DENSITY = 0.000015;

GLStarfield::GLStarfield(Point const size) {
  point_layers = glGenLists(NUM_REAR_LAYERS + NUM_FRONT_LAYERS + 1);
  int red, green;
  for(int i = 0; i < NUM_REAR_LAYERS + NUM_FRONT_LAYERS + 1; i++) {
    glNewList(point_layers+i, GL_COMPILE);
    glBegin(GL_POINTS);

    int num_stars = size.x()*size.y()*STAR_DENSITY;
    for(int j = 0; j < num_stars; j++) {
      red = rand()%100;
      green = red > 0 ? rand()%red : 0;
      float r_f = red / 100.0f;
      float g_f = green / 100.0f;
      float b_f = rand()%100 / 100.0f;
      float a_f = rand()%50 / 100.0f + 0.2f;
      float x_f = (float)(rand()%(int)size.x());
      float y_f = (float)(rand()%(int)size.y());
      float z_f = (float)((i - NUM_REAR_LAYERS) * 100);
      glColor4f(r_f, g_f, b_f, a_f);
      glVertex3f(x_f, y_f, z_f);
      // Store rear-layer and middle-layer stars for lensing
      if (i <= NUM_REAR_LAYERS) {
        rear_stars.push_back({x_f, y_f, z_f, r_f, g_f, b_f, a_f});
      }
    }
    glEnd();
    glEndList();
  }
}

GLStarfield::~GLStarfield() {
  glDeleteLists(point_layers, NUM_REAR_LAYERS + NUM_FRONT_LAYERS + 1);
}

void GLStarfield::draw_rear(Point const viewpoint) const {
  glPointSize(2.0f * Typer::scale);
  for(int i = 0; i < NUM_REAR_LAYERS; i++) {
    glCallList(point_layers+i);
  }
  glCallList(point_layers+NUM_REAR_LAYERS);
}

void GLStarfield::draw_front(Point const viewpoint) const {
  glPointSize(2.0f * Typer::scale);
  for(int i = 0; i < NUM_FRONT_LAYERS; i++) {
    glCallList(point_layers + NUM_REAR_LAYERS + 1 + i);
  }
}

void GLStarfield::draw_stars_near(float cx, float cy, float radius) const {
  float r2 = radius * radius;
  glPointSize(2.0f * Typer::scale);
  glBegin(GL_POINTS);
  for (size_t k = 0; k < rear_stars.size(); k++) {
    StarPoint const &s = rear_stars[k];
    float dx = s.x - cx;
    float dy = s.y - cy;
    float dist2 = dx * dx + dy * dy;
    if (dist2 >= r2) continue;
    float dist = sqrtf(dist2);
    float nx, ny, shift;
    if (dist > 0.001f) {
      nx = dx / dist;
      ny = dy / dist;
      // Shift stars radially outward; strongest at centre, zero at edge
      shift = radius * 0.5f * (1.0f - dist / radius);
    } else {
      nx = 1.0f; ny = 0.0f;
      shift = radius * 0.5f;
    }
    glColor4f(s.r, s.g, s.b, s.a);
    glVertex3f(s.x + nx * shift, s.y + ny * shift, s.z);
  }
  glEnd();
}
