#include "world.h"

World::World(int width, int height, int num_players) {
  time_between_steps = step_size;
  num_players = player_count;

  enemies = new std::list<GLShip*>;
  players = new std::list<GLShip*>;

  WrappedPoint::set_boundaries(world);

  starfield = new GLStarfield(world);

  gameworld = glGenLists(1);

  time_until_next_step = 0;
  num_frames = 0;
  
  // GLShip* object = new GLShip(-world.x()*3/4,-world.y()*3/4);
  // object->set_keys('a','d','w',' ','s','x');
  // players->push_back(object);
  // 
  // if (player_count == 2) {
  //   object = new GLCar(world.x()*3/4,world.y()*3/4);
  //   object->set_keys('j','l','i','/','k',',');
  //   players->push_back(object);
  // }

  //TODO: GLstation uses players.size() to determine number of ships in first wave
  station = new GLStation(enemies, players, num_players);
}

World::~World() {
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
  delete station;
  delete starfield;

  glDeleteLists(gameworld, 1);
}

bool World::is_single() const {
  return players->size() == 1;
}

void World::toggle_pause() {
  running = !running;
}

void World::draw(bool minimap) const {
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
  station->draw(minimap);
}

void World::draw(GLShip *glship) const {
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  int width_scale = is_single() ? 1 : 2;
  gluOrtho2D(-window.x()/width_scale, window.x()/width_scale, -window.y(), window.y());
  glMatrixMode(GL_MODELVIEW);

  glLoadIdentity();

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

  /* Draw the score */
  Typer::draw(window.x()/width_scale-40, window.y()-20, glship->ship->score, 20);
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

void World::tick(int delta) {
  if (!running) {
    last_tick += delta;
    return;
  }
  
  time_until_next_step -= delta;

  num_frames++;

  std::list<GLShip*>::iterator o, o2;
  while(time_until_next_step <= 0) {
    station->step(step_size);
    for(o = players->begin(); o != players->end(); o++) {
      (*o)->step(step_size);

      station->collide((*o)->ship);
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

void World::keyboard(unsigned char key, int x, int y) {
  std::list<GLShip*>::iterator object;
  for(object = players->begin(); object != players->end(); object++) {
    (*object)->input(key);
  }
}

void World::keyboard_up(unsigned char key, int x, int y) {
  if (key == '=' && time_between_steps > 1) time_between_steps--;
  if (key == '-') time_between_steps++;
  if (key == '0') time_between_steps = step_size;
  if (key == 'p') toggle_pause();
  std::list<GLShip*>::iterator object;
  for(object = players->begin(); object != players->end(); object++) {
    (*object)->input(key, false);
  }
}