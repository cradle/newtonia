#include "asteroid_drawer.h"
#include "asteroid.h"
#include "typer.h"
#include <math.h>

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/glut.h>
#endif

#include <iostream>

const int AsteroidDrawer::number_of_segments = 7;

void AsteroidDrawer::draw(Asteroid const *object, bool is_minimap) {
  if(object->alive) {
    glPushMatrix();
    glTranslatef(object->position.x(), object->position.y(), 0.0f);
    glScalef(object->radius, object->radius, 1.0f);
    glRotatef(object->rotation, 0.0f, 0.0f, 1.0f);
    if(object->invincible) {
      glColor4f(0.5f, 0.5f, 0.5f, 0.5f);
    } else {
      glColor3f(0.0f, 0.0f, 0.0f);
    }
    if(is_minimap) {
      glLineWidth(1.0f);
    } else {
      glLineWidth(2.5f);
    }
    glBegin(GL_POLYGON);
    float segment_size = 360.0/number_of_segments, d;
    for (float i = 0.0; i < 360.0; i+= segment_size) {
      d = i*M_PI/180;
      glVertex2f(cos(d),sin(d));
    }
    glEnd();
    if(object->invincible) {
      glColor4f(0.8f, 0.8f, 0.8f, 0.8f);
    } else {
      glColor3f(1.0f, 1.0f, 1.0f);
    }
    glBegin(GL_LINE_LOOP);
    for (float i = 0.0; i < 360.0; i+= segment_size) {
      d = i*M_PI/180;
      glVertex2f(cos(d),sin(d));
    }
    glEnd();
    glPopMatrix();
  } else if(!is_minimap) {
    draw_debris(object->debris); 
    Typer::draw(object->position.x(), object->position.y(), object->value, 15);
  }
}

void AsteroidDrawer::draw_debris(list<Particle> debris) {
  glPointSize(3.0f);
  glBegin(GL_POINTS);
  for(list<Particle>::iterator d = debris.begin(); d != debris.end(); d++) {
    glColor4f(1.0f, 1.0f, 1.0f, rand()/(1.0f*(float)RAND_MAX) * d->aliveness()/2.0f + d->aliveness()/2.0f);
		glVertex2fv(d->position);
  }
	glEnd();
}