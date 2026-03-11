#include "overlay.h"
#include "../glship.h"
#include "../glgame.h"
#include "../typer.h"

#include "../gl_compat.h"
#include <cstdio>

const float Overlay::CORNER_INSET = 0.05f;

void Overlay::draw(const GLGame *glgame, const GLShip *glship) {
  title_text(glgame, glship);
  level(glgame, glship);
  score(glgame, glship);
  keymap(glgame, glship);
  level_cleared(glgame, glship);
  lives(glgame, glship);
  weapons(glgame, glship);
  temperature(glgame, glship);
  respawn_timer(glgame, glship);
  paused(glgame, glship);
}

void Overlay::paused(const GLGame *glgame, const GLShip *glship) {
  if(!glgame->running && !glship->show_help) {
    Typer::draw_centered(0, 30, "Paused", 25);
    Typer::draw_centered(0, -40, "press p to unpause", 8);
  }
}


void Overlay::level(const GLGame *glgame, const GLShip *glship) {
  char buf[16];
  snprintf(buf, sizeof(buf), "LEVEL %d", glgame->generation + 1);
  float vh = Typer::scaled_window_height / glgame->num_y_viewports();
  Typer::draw_centered(0, vh - 20 - CORNER_INSET, buf, 12);
}

void Overlay::score(const GLGame *glgame, const GLShip *glship) {
  //FIX: Window encapsulation? Players size encapsulation?
  float vw = Typer::scaled_window_width / glgame->num_x_viewports();
  float vh = Typer::scaled_window_height / glgame->num_y_viewports();
  Typer::draw(vw - 40 - CORNER_INSET, vh - 20 - CORNER_INSET, glship->ship->score, 20);
  if(glship->ship->multiplier() > 1) {
    Typer::draw(vw - 35 - CORNER_INSET, vh - 92 - CORNER_INSET, "x", 15);
    Typer::draw(vw - 65 - CORNER_INSET, vh - 80 - CORNER_INSET, glship->ship->multiplier(), 20);
  }
}

void Overlay::level_cleared(const GLGame *glgame, const GLShip *glship) {
  if(glgame->running && glgame->level_cleared && glship->ship->is_alive() && glship->ship->lives > 0) {
    Typer::draw_centered(0, 150, "CLEARED", 50);
    Typer::draw_centered(0, -60, (glgame->time_until_next_generation / 1000)+1, 20); // respawn AT 0
  }
}

void Overlay::lives(const GLGame *glgame, const GLShip *glship) {
  Typer::draw_lives(Typer::scaled_window_width/glgame->num_x_viewports()-40-CORNER_INSET, -Typer::scaled_window_height/glgame->num_y_viewports()+70+CORNER_INSET, glship, 18);
}

void Overlay::weapons(const GLGame *glgame, const GLShip *glship) {
  glPushMatrix();
  glTranslatef(-Typer::window_width/glgame->num_x_viewports()+CORNER_INSET, Typer::window_height/glgame->num_y_viewports()-CORNER_INSET, 0.0f);
  glship->draw_weapons();
  glPopMatrix();
}

void Overlay::temperature(const GLGame *glgame, const GLShip *glship) {
  glPushMatrix();
  glTranslatef(-Typer::scaled_window_width/glgame->num_x_viewports()+30+CORNER_INSET, -Typer::scaled_window_height/glgame->num_y_viewports()+15+CORNER_INSET, 0.0f);
  glPushMatrix();
  glScalef(30,30,1);
  glship->draw_temperature();
  glPopMatrix();
  glTranslatef(42.0f, 147.0f, 0.0f);
  glScalef(10,10,1);
  glship->draw_temperature_status();
  glPopMatrix();
}

void Overlay::respawn_timer(const GLGame *glgame, const GLShip *glship) {
  if(glgame->running && !glship->show_help) {
    glPushMatrix();
    glScalef(20,20,1);
    glship->draw_respawn_timer();
    glPopMatrix();
  }
}

void Overlay::keymap(const GLGame *glgame, const GLShip *glship) {
  if(glship->show_help) {
    glPushMatrix();
    glship->draw_keymap();
    glPopMatrix();
  }
}

void Overlay::title_text(const GLGame *glgame, const GLShip *glship) {
  Ship* p1 = glgame->players->front()->ship;
  if(glgame->players->size() < 2) {
    if((glgame->current_time/1400) % 2) {
      if(p1->is_alive() || p1->lives > 0) {
#ifndef __ANDROID__
        Typer::draw_centered(Typer::scaled_window_width/2, Typer::scaled_window_height-10, "player 2 press enter to join", 8);
#endif
      } else {
        Typer::draw_centered(0, Typer::window_height-10, "return to menu with ESC", 8);
      }
    }
    if(glship->controller == NULL) {
      if(glship->show_help) {
        Typer::draw_centered(-1*Typer::scaled_window_width/2, Typer::scaled_window_height-10, "hide controls with F1", 8);
      } else if ((glgame->current_time)/12000 % 2) {
        Typer::draw_centered(-1*Typer::scaled_window_width/2, Typer::scaled_window_height-10, "show controls with F1", 8);
      }
    }
  } else {
    if(glgame->friendly_fire) {
      Typer::draw_centered(0, Typer::scaled_window_height/glgame->num_y_viewports()-40, "friendly fire on", 8);
    }
    if(glship->controller == NULL) {
      if(p1 == glship->ship) {
        if(glship->show_help) {
          Typer::draw_centered(0, Typer::scaled_window_height/glgame->num_y_viewports()-10, "hide controls with F1", 8);
        } else if ((glgame->current_time)/12000 % 2) {
          Typer::draw_centered(0, Typer::scaled_window_height/glgame->num_y_viewports()-10, "show controls with f1", 8);
        }
      } else {
        if(glship->show_help) {
          Typer::draw_centered(0, Typer::scaled_window_height/glgame->num_y_viewports()-10, "hide controls with F8", 8);
        } else if ((glgame->current_time)/12000 % 2) {
          Typer::draw_centered(0, Typer::scaled_window_height/glgame->num_y_viewports()-10, "show controls with f8", 8);
        }
      }
    }
  }
  if(!glgame->running && glship->show_help) {
    Typer::draw_centered(0, Typer::scaled_window_height/glgame->num_y_viewports()-80, "press p to unpause", 8);
  }
}
