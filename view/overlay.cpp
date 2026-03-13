#include "overlay.h"
#include "../glship.h"
#include "../glgame.h"
#include "../typer.h"
#include "../touch_controls.h"

#include "../gl_compat.h"
#include <cstdio>
#include <cmath>

const float Overlay::CORNER_INSET = 35.0f;

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
  touch_controls(glgame, glship);
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
#if !defined(__ANDROID__) && !defined(__IOS__)
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
    float vhb = -Typer::scaled_window_height/glgame->num_y_viewports();
    if(glgame->friendly_fire) {
      Typer::draw_centered(0, vhb+30, "friendly fire on", 8);
    }
    if(glship->controller == NULL) {
      if(p1 == glship->ship) {
        if(glship->show_help) {
          Typer::draw_centered(0, vhb+60, "hide controls with F1", 8);
        } else if ((glgame->current_time)/12000 % 2) {
          Typer::draw_centered(0, vhb+60, "show controls with F1", 8);
        }
      } else {
        if(glship->show_help) {
          Typer::draw_centered(0, vhb+60, "hide controls with F8", 8);
        } else if ((glgame->current_time)/12000 % 2) {
          Typer::draw_centered(0, vhb+60, "show controls with F8", 8);
        }
      }
    }
  }
  if(!glgame->running && glship->show_help) {
    Typer::draw_centered(0, Typer::scaled_window_height/glgame->num_y_viewports()-80, "press p to unpause", 8);
  }
}

// Draw a circle (outline or filled) in the current OpenGL orthogonal context.
void Overlay::draw_circle(float cx, float cy, float r, int segs, bool filled) {
  if(filled) {
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
  } else {
    glBegin(GL_LINE_LOOP);
  }
  for(int i = 0; i <= segs; i++) {
    float angle = 2.0f * (float)M_PI * (float)i / (float)segs;
    glVertex2f(cx + r * cosf(angle), cy + r * sinf(angle));
  }
  glEnd();
}

void Overlay::touch_controls(const GLGame *glgame, const GLShip *glship) {
#if defined(__ANDROID__) || defined(__IOS__)
  // Only render for the primary (first) player.
  if(glgame->players->front() != glship) return;

  float pw = (float)Typer::window_width;
  float ph = (float)Typer::window_height;

  auto ox = [&](float px) { return 2.0f * px - pw; };
  auto oy = [&](float py) { return ph - 2.0f * py; };
  auto sr = [&](float r)  { return 2.0f * r; };

  const TouchControlsState &tc = g_touch_controls;

  // ---- Virtual joystick ----
  float jox = ox(tc.joy_hint_cx);
  float joy = oy(tc.joy_hint_cy);
  float jr  = sr(tc.joy_radius);

  if(tc.joy_active) {
    // Active: draw outer ring at floating base position
    float bx = ox(tc.joy_cx);
    float by = oy(tc.joy_cy);
    glColor4f(0.5f, 0.65f, 1.0f, 0.75f);
    draw_circle(bx, by, jr, 32, false);

    // Nub: note y is flipped (screen-down = overlay-up in the nub offset)
    float nx_off =  tc.joy_nx * jr;
    float ny_off = -tc.joy_ny * jr;  // flip: screen y increases downward
    glColor4f(0.7f, 0.85f, 1.0f, 0.90f);
    draw_circle(bx + nx_off, by + ny_off, jr * 0.38f, 32, true);
  } else {
    // Inactive: hint ring at home position
    glColor4f(0.4f, 0.55f, 1.0f, 0.55f);
    draw_circle(jox, joy, jr, 32, false);
    // Small centre dot
    glColor4f(0.4f, 0.55f, 1.0f, 0.40f);
    draw_circle(jox, joy, jr * 0.25f, 20, true);
  }

  // ---- Shoot button ----
  {
    float bx = ox(tc.shoot_cx);
    float by = oy(tc.shoot_cy);
    float br = sr(tc.shoot_radius);
    float alpha_fill    = tc.shoot_pressed ? 0.55f : 0.25f;
    float alpha_outline = tc.shoot_pressed ? 0.95f : 0.70f;
    glColor4f(1.0f, 0.35f, 0.35f, alpha_fill);
    draw_circle(bx, by, br, 28, true);
    glColor4f(1.0f, 0.35f, 0.35f, alpha_outline);
    draw_circle(bx, by, br, 28, false);
  }

  // ---- Mine button ----
  {
    float bx = ox(tc.mine_cx);
    float by = oy(tc.mine_cy);
    float br = sr(tc.mine_radius);
    float alpha_fill    = tc.mine_pressed ? 0.55f : 0.25f;
    float alpha_outline = tc.mine_pressed ? 0.95f : 0.70f;
    glColor4f(0.35f, 0.6f, 1.0f, alpha_fill);
    draw_circle(bx, by, br, 28, true);
    glColor4f(0.35f, 0.6f, 1.0f, alpha_outline);
    draw_circle(bx, by, br, 28, false);
  }
#endif // __ANDROID__ || __IOS__
}
