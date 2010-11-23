#include "glgame.h"
#include "glship.h"
#include "glcar.h"
#include "glstarfield.h"
#include "wrapped_point.h"
#include "menu.h"
#include "state.h"
#include "asteroid.h"
#include "asteroid_drawer.h"
#include "object.h"
#include "grid.h"
#include "view/overlay.h"

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/glut.h>
#endif

#include <iostream>
#include <list>

const int GLGame::default_world_width = 2000;
const int GLGame::default_world_height = 2000;
const int GLGame::default_num_asteroids = 1;
const int GLGame::extra_num_asteroids = 5;

GLGame::GLGame() :
  State(),
  world(Point(default_world_width, default_world_height)),
  running(true),
  friendly_fire(false),
  grid(Grid(world, Point(Asteroid::max_radius*2,Asteroid::max_radius*2))) {
  time_between_steps = step_size;
  level_cleared = false;

  enemies = new std::list<GLShip*>;
  players = new std::list<GLShip*>;
  objects = new std::list<Asteroid*>;

  WrappedPoint::set_boundaries(world);

  starfield = new GLStarfield(world);
  
  GLShip *object = new GLShip(true);
  object->set_keys('a','d','w',' ','s','x','q');
  players->push_back(object);

  rearstars = glGenLists(1);
  gameworld = glGenLists(1);
  frontstars = glGenLists(1);

  time_until_next_step = 0;
  num_frames = 0;
  
  generation = 0;
  add_asteroids();

  station = NULL;//new GLStation(enemies, players);
}

GLGame::~GLGame() {
  //TODO: Make erase, use boost::ptr_list? something better
  // std::erase(std::remove_if(v.begin(),v.end(),true), v.end());
  while(!players->empty()) {
    delete players->back();
    players->pop_back();
  }
  delete players;
  while(!enemies->empty()) {
    delete enemies->back();
    enemies->pop_back();
  }
  delete enemies;
  while(!objects->empty()) {
    delete objects->back();
    objects->pop_back();
  }
  delete objects;
  delete starfield;
  if(station != NULL)
    delete station;

  glDeleteLists(gameworld, 1);
}

void GLGame::add_asteroids() {
  while(Asteroid::num_killable < (default_num_asteroids + generation * extra_num_asteroids)) {
    objects->push_back(new Asteroid(false)); 
    objects->push_front(new Asteroid(true)); 
  }
}

void GLGame::toggle_pause() {
  running = !running;
}

bool GLGame::cleared() const {
  return level_cleared;
}

void GLGame::tick(int delta) {
  if (!running) {
    last_tick += delta;
    return;
  }

  time_until_next_step -= delta;

  num_frames++;

  if(Asteroid::num_killable == 0) {
    if(!level_cleared) {
      level_cleared = true;
      time_until_next_generation = 5000;
    } else if (time_until_next_generation > 0) {
      time_until_next_generation -= delta;
    } else {
      generation++;
      if(generation >= 10) {
        if(station != NULL)
          delete station;
        station = new GLStation(enemies, players);
        world += Point(3000, 3000);
      } else {
        world += Point(100, 100);
      }
      grid = Grid(world, Point(Asteroid::max_radius*2,Asteroid::max_radius*2));
      if(station != NULL) {
        station->reset();
      }
      delete starfield;
      starfield = new GLStarfield(world);
      WrappedPoint::set_boundaries(world);
      add_asteroids();
      std::list<GLShip*>::iterator o;
      for(o = players->begin(); o != players->end(); o++) {
        (*o)->ship->respawn(false);
      }
      level_cleared = false;
    }
  }

  std::list<GLShip*>::iterator o, o2;
  while(time_until_next_step <= 0) {
	/* STEP EVERYTHING */
	
    if(station != NULL) {
      station->step(step_size);
    }

    std::list<Asteroid*>::iterator oi;
    for(oi = objects->begin(); oi != objects->end(); oi++) {
      (*oi)->step(step_size);
    }

    for(o = players->begin(); o != players->end(); o++) {
      (*o)->step(step_size);
    }

    for(o = enemies->begin(); o != enemies->end(); o++) {
      (*o)->step(step_size);
    }

    /* UPDATE COLLISION MAP */

    grid.update((std::list<Object *>*)objects);

  /* COLLIDE EVERYTHING */
    for(o = players->begin(); o != players->end(); o++) {
      (*o)->ship->collide_grid(grid);
    }

    oi = objects->begin();
    while(oi != objects->end()) {
      (*oi)->add_children(objects);
      if((*oi)->is_removable()) {
        delete *oi;
        oi = objects->erase(oi);
      } else {
        oi++;
      }
    }

    for(o = players->begin(); o != players->end(); o++) {
      if(friendly_fire) {
        for(o2 = o; o2 != players->end(); o2++) {
          if(*o != *o2) {
            GLShip::collide(*o, *o2);
          }
        }
      }
      for(o2 = enemies->begin(); o2 != enemies->end(); o2++) {
        GLShip::collide(*o, *o2);
      }
    }

    o = enemies->begin();
    while(o != enemies->end()) {
      if((*o)->is_removable()) {
        delete *o;
        o = enemies->erase(o);
      } else {
        o++;
      }
    }

    time_until_next_step += time_between_steps;
  }
  /* Display FPS */
  // std::cout << (num_frames*1000 / current_time) << std::endl;
}

void GLGame::draw_objects(float direction, bool minimap) const {
  std::list<Asteroid*>::iterator oi;
  for(oi = objects->begin(); oi != objects->end(); oi++) {
    //TODO: make AsteroidController (???), which joins model and view together
    AsteroidDrawer::draw(*oi, direction, minimap);
  }

  std::list<GLShip*>::iterator o;
  for(o = players->begin(); o != players->end(); o++) {
    glPushMatrix();
    (*o)->draw(minimap);
    glPopMatrix();
  }
  for(o = enemies->begin(); o != enemies->end(); o++) {
    glPushMatrix();
    (*o)->draw(minimap);
    glPopMatrix();
  }

  if(station != NULL) station->draw(minimap);
}

void GLGame::draw(void) {
  glClear(GL_COLOR_BUFFER_BIT);

  if(players->size() == 0) {
    draw_world();
  }
  else {
    if(players->size() > 0) {
      draw_world(players->front(), true);
    }
    if(players->size() > 1) {
      draw_world(players->back(), false);
    }
    //Draw map after - for partial translucency
    draw_map();
  }
}

void GLGame::setup_perspective(GLShip *glship) const {
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluPerspective(glship->view_angle(), window.x()/num_x_viewports()/window.y(), 100.0f, 2000.0f);
  glMatrixMode(GL_MODELVIEW); 
}

int GLGame::num_x_viewports() const {
  return (players->size() == 0) ? 1 : players->size();
}

void GLGame::setup_orthogonal() const {
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluOrtho2D(-window.x()/num_x_viewports(), window.x()/num_x_viewports(), -window.y(), window.y());
  glMatrixMode(GL_MODELVIEW);
}

void GLGame::setup_viewport(bool primary) const {
  glLoadIdentity();
  glViewport((primary ? 0 : (window.x()/2)), 0, window.x()/num_x_viewports(), window.y());
}

void GLGame::draw_world(GLShip *glship, bool primary) const {
  setup_perspective(glship);
  setup_viewport(primary);
  gluLookAt(0.0f, 0.0f, 1000.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f );
  draw_perspective(glship);
  setup_orthogonal();
  setup_viewport(primary);
  Overlay::draw(this, glship);
}

void GLGame::draw_perspective(GLShip *glship) const {
  /* Draw the world */
  // Store the rendered world in a display list
  Point position = (glship == NULL) ? Point(0,0) : glship->ship->position;
  float direction = (glship == NULL || !glship->rotate_view()) ? 0.0f : glship->camera_facing();
  glNewList(rearstars, GL_COMPILE);
    glTranslatef(-position.x(), -position.y(), 0.0f);
    starfield->draw_rear(position);
  glEndList();
  glNewList(gameworld, GL_COMPILE);
    glTranslatef(-position.x(), -position.y(), 0.0f);
    draw_objects(direction);
  glEndList();
  glNewList(frontstars, GL_COMPILE);
    glTranslatef(-position.x(), -position.y(), 0.0f);
    starfield->draw_front(position);
  glEndList();
  // Draw the world tesselated
  ///TODO: DRY this up a bit
  for(int x = -1; x <= 1; x++) {
    for(int y = -1; y <= 1; y++) {
      glPushMatrix();
      glRotatef(direction, 0.0f, 0.0f, 1.0f);
      glTranslatef(world.x()*x, world.y()*y, 0.0f);
      glCallList(rearstars);
      glPopMatrix();
    }
  }
  for(int x = -1; x <= 1; x++) {
    for(int y = -1; y <= 1; y++) {
      glPushMatrix();
      glRotatef(direction, 0.0f, 0.0f, 1.0f);
      glTranslatef(world.x()*x, world.y()*y, 0.0f);
      glCallList(gameworld);
      glPopMatrix();
    }
  }
  for(int x = -1; x <= 1; x++) {
    for(int y = -1; y <= 1; y++) {
      glPushMatrix();
      glRotatef(direction, 0.0f, 0.0f, 1.0f);
      glTranslatef(world.x()*x, world.y()*y, 0.0f);
      glCallList(frontstars);
      glPopMatrix();
    }
  }
}

void GLGame::draw_map() const {
  float minimap_size = window.y()/4;

  if(players->size() > 1) {
    /* DRAW CENTER LINE */
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(-window.x(), window.x(), -window.y(), window.y());
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glViewport(0, 0, window.x(), window.y());
    glBegin(GL_LINES);
    glColor4f(1,1,1,0.5);
    glVertex2f(0,-window.y());
    glVertex2f(0,-minimap_size);
    glVertex2f(0, minimap_size);
    glVertex2f(0, window.y());
    glEnd();
  }

  /* MINIMAP */
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluOrtho2D(0, world.x(), 0, world.y());
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  if (players->size() == 1) {
    glViewport(window.x()/2 - minimap_size/2, 0, minimap_size, minimap_size);
  } else {
    glViewport(window.x()/2 - minimap_size/2, window.y()/2 - minimap_size/2, minimap_size, minimap_size);
  }
  
  /* BLACK BOX OVER MINIMAP */
  glColor4f(0.0f,0.0f,0.0f,0.8f);
  glBegin(GL_POLYGON);
    glVertex2i(  0, world.y());
    glVertex2i(  world.x(), world.y());
    glVertex2i(  world.x(), 0);
    glVertex2i(  0, 0);
  glEnd();
  /* LINE AROUND MINIMAP */
  glColor3f(0.5f,0.5f,0.5f);
  glBegin(GL_LINE_LOOP);
    glVertex2i( 0, world.y());
    glVertex2i(  world.x(), world.y());
    glVertex2i(  world.x(),0);
    glVertex2i( 0,0);
  glEnd();
  
  /* DRAW THE LEVEL */
  Typer::draw(-world.x()+world.x()/15.0f, world.y()-world.y()/15.0f, "LEVEL", world.x()/15.0f);
  Typer::draw(world.x()-world.x()/15.0f*2.0f, world.y()-world.y()/15.0f, generation, world.x()/15.0f);
  
  if(station != NULL) {
    Typer::draw(-world.x()+world.x()/15.0f, -world.y()+world.y()/15.0f*3.0f, "WAVE", world.x()/15.0f);
    Typer::draw(world.x()-world.x()/15.0f*2.0f, -world.y()+world.y()/15.0f*3.0f, station->level(), world.x()/15.0f);
  }
  
  glPushMatrix();
  draw_objects(0.0f, true);
  glPopMatrix();
}

void GLGame::keyboard (unsigned char key, int x, int y) {
  if (!running)
    return;
  
  std::list<GLShip*>::iterator object;
  for(object = players->begin(); object != players->end(); object++) {
    (*object)->input(key);
  }
}

void GLGame::keyboard_up (unsigned char key, int x, int y) {
  if (key == 'n') {
      level_cleared = true;
      time_until_next_generation = 0;
      while(!objects->empty()) {
        delete objects->back();
        objects->pop_back();
      }
  }
  if (key == 'g') {
    friendly_fire = !friendly_fire;
  }
  if (key == '=' && time_between_steps > 1) time_between_steps--;
  if (key == '-') time_between_steps++;
  if (key == '0') time_between_steps = step_size;
  if (key == 'p') toggle_pause();
  if (key == 13 && players->size() < 2) {
    GLShip* object = new GLCar(true);
    object->set_keys('j','l','i','/','k',',','u');
    players->push_back(object);
  }
  if (key == 27) request_state_change(new Menu());
  
  if (!running)
    return;
  
  std::list<GLShip*>::iterator object;
  for(object = players->begin(); object != players->end(); object++) {
    (*object)->input(key, false);
  }
}