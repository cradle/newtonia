#include "glgame.h"
#include "glship.h"
#include "glenemy.h"
#include "glcar.h"
#include "glstarfield.h"
#include "wrapped_point.h"
#include "menu.h"
#include "state.h"
#include "asteroid.h"
#include "asteroid_drawer.h"
#include "object.h"

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

const int GLGame::default_world_width = 5000;
const int GLGame::default_world_height = 5000;
const int GLGame::default_num_asteroids = 50;

GLGame::GLGame(int player_count, bool has_station) : 
  State(), 
  world(Point(default_world_width, default_world_height)), 
  running(true) {
  time_between_steps = step_size;
  num_players = player_count;

  enemies = new std::list<GLShip*>;
  players = new std::list<GLShip*>;
  objects = new std::list<Asteroid*>;

  WrappedPoint::set_boundaries(world);

  starfield = new GLStarfield(world);

  gameworld = glGenLists(1);

  time_until_next_step = 0;
  num_frames = 0;
  
  GLShip* object = new GLShip(-world.x()*3/4,-world.y()*3/4);
  object->set_keys('a','d','w',' ','s','x');
  players->push_back(object);
  
  if (player_count == 2) {
    object = new GLCar(world.x()*3/4,world.y()*3/4);
    object->set_keys('j','l','i','/','k',',');
    players->push_back(object);
  }

  int num_asteroids;
  if(has_station) {
    num_asteroids = player_count * 2;
  } else {
    num_asteroids = default_num_asteroids * player_count;
  }
  for(int i = 0; i < num_asteroids; i++) {
    objects->push_back(new Asteroid());
  }
  
  //GLstation uses players.size() to determine number of ships in first wave
  station = has_station ? (new GLStation(enemies, players)) : NULL;
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
  delete starfield;
  if(station != NULL)
    delete station;

  glDeleteLists(gameworld, 1);
}

void GLGame::toggle_pause() {
  running = !running;
}

void GLGame::tick(int delta) {
  if (!running) {
    last_tick += delta;
    return;
  }
  
  time_until_next_step -= delta;

  num_frames++;

  std::list<GLShip*>::iterator o, o2;
  while(time_until_next_step <= 0) {
    if(station != NULL) station->step(step_size); 
    
    std::list<Asteroid*>::iterator oi, ol;
    for(oi = objects->begin(); oi != objects->end(); oi++) {
      (*oi)->step(step_size);
     
      for(o = players->begin(); o != players->end(); o++) {
        if((*o)->ship->collide_asteroid(*oi)) {
          (*oi)->add_children(objects);
        }
      }
    }
    
    oi = objects->begin();
    while(oi != objects->end()) {
      if((*oi)->is_removable()) {
        delete *oi;
        oi = objects->erase(oi);
      } else {
        oi++;
      }
    }
    
    for(o = players->begin(); o != players->end(); o++) {
      (*o)->step(step_size);
    }
    
    for(o = enemies->begin(); o != enemies->end(); o++) {
      (*o)->step(step_size);
    }

    for(o = players->begin(); o != players->end(); o++) {
      for(o2 = o; o2 != players->end(); o2++) {
        if(*o != *o2) {
          GLShip::collide(*o, *o2);
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

void GLGame::draw_objects(bool minimap) const {
  std::list<Asteroid*>::iterator oi;
  for(oi = objects->begin(); oi != objects->end(); oi++) {
    //TODO: make AsteroidController (???), which joins model and view together
    AsteroidDrawer::draw(*oi, minimap);
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

  //TODO: Don't hardcode this like this
  draw_world(players->front(), true);
  if (!is_single()) {
    draw_world(players->back(), false);
  } 
  draw_map();
}

void GLGame::draw_world(GLShip *glship, bool primary) const {
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  int width_scale = is_single() ? 1 : 2;
  gluOrtho2D(-window.x()/width_scale, window.x()/width_scale, -window.y(), window.y());
  glMatrixMode(GL_MODELVIEW);

  glLoadIdentity();
  glViewport((primary ? 0 : (window.x()/2)), 0, window.x()/width_scale, window.y());

  /* Draw the world */
  // Store the rendered world in a display list
  glNewList(gameworld, GL_COMPILE);
    glTranslatef(-glship->ship->position.x(), -glship->ship->position.y(), 0.0f);
    starfield->draw_rear(glship->ship->position);
    draw_objects();
    starfield->draw_front(glship->ship->position);
  glEndList();
  // Draw the world tesselated
  for(int x = -1; x <= 1; x++) {
    for(int y = -1; y <= 1; y++) {
      glPushMatrix();
      glTranslatef(world.x()*2*x, world.y()*2*y, 0.0f);
      glCallList(gameworld);
      glPopMatrix();
    }
  }

  if(players->size() == 1) {
    Typer::draw(80, window.y()-30, "press 2 to join", 10);
  }

  /* Draw the score */
  Typer::draw(window.x()/width_scale-40, window.y()-20, glship->ship->score, 20);
  if(glship->ship->multiplier() > 1) {
    Typer::draw(window.x()/width_scale-35, window.y()-92, "x", 15);
    Typer::draw(window.x()/width_scale-65, window.y()-80, glship->ship->multiplier(), 20);
  }
  /* Draw the life count */
  Typer::draw_lives(window.x()/width_scale-40,-window.y()+70, glship, 18);
  //TODO: Move name into ship object.
  const char *name = primary ? "Player 1" : "Player 2";
  Typer::draw(-window.x()/width_scale+30,window.y()-20,name,20);
  glPushMatrix();
  glTranslatef(-window.x()/width_scale+30, -window.y()+15, 0.0f);
  glPushMatrix();
  glScalef(30,30,1);
  glship->draw_temperature();
  glPopMatrix();
  glTranslatef(42.0f, 147.0f, 0.0f);
  glScalef(10,10,1);
  glship->draw_temperature_status();
  glPopMatrix();
  
  glPushMatrix();
  glScalef(20,20,1);
  glship->draw_respawn_timer();
  glPopMatrix();
}

void GLGame::draw_map() const {
  float minimap_size = window.y()/4;

  if(!is_single()) {
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
  gluOrtho2D(-world.x(), world.x(), -world.y(), world.y());
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  if (is_single()) {
    glViewport(window.x()/2 - minimap_size/2, 0, minimap_size, minimap_size);
  } else {
    glViewport(window.x()/2 - minimap_size/2, window.y()/2 - minimap_size/2, minimap_size, minimap_size);
  }
  glColor4f(0.0f,0.0f,0.0f,0.8f);
  /* BLACK BOX OVER MINIMAP */
  glBegin(GL_POLYGON);
    glVertex2i( -world.x(), world.y());
    glVertex2i(  world.x(), world.y());
    glVertex2i(  world.x(),-world.y());
    glVertex2i( -world.x(),-world.y());
  glEnd();
  /* LINE AROUND MINIMAP */
  glColor3f(0.5f,0.5f,0.5f);
  glBegin(GL_LINE_LOOP);
    glVertex2i( -world.x(), world.y());
    glVertex2i(  world.x(), world.y());
    glVertex2i(  world.x(),-world.y());
    glVertex2i( -world.x(),-world.y());
  glEnd();
  glPushMatrix();
  draw_objects(true);
  glPopMatrix();
}

void GLGame::keyboard (unsigned char key, int x, int y) {
  std::list<GLShip*>::iterator object;
  for(object = players->begin(); object != players->end(); object++) {
    (*object)->input(key);
  }
}

bool GLGame::is_single() const {
  return players->size() == 1;
}

void GLGame::keyboard_up (unsigned char key, int x, int y) {
  if (key == '=' && time_between_steps > 1) time_between_steps--;
  if (key == '-') time_between_steps++;
  if (key == '0') time_between_steps = step_size;
  if (key == 'p') toggle_pause();
  if (key == '2' && (players->size() == 1)) {
    GLShip* object = new GLShip(world.x()*3/4,world.y()*3/4);
    object->set_keys('j','l','i','/','k',',');
    players->push_back(object);
  }
  if (key == 27) request_state_change(new Menu());
  std::list<GLShip*>::iterator object;
  for(object = players->begin(); object != players->end(); object++) {
    (*object)->input(key, false);
  }
}