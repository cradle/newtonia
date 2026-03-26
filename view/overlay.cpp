#include "overlay.h"
#include "../glship.h"
#include "../glgame.h"
#include "../typer.h"
#include "../asteroid.h"
#include "../touch_controls.h"

#include "../gl_compat.h"
#include <cstdio>
#include <cmath>

const float Overlay::CORNER_INSET = 55.0f;

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
  edge_indicators(glgame, glship);
  god_mode_indicator(glgame, glship);
}

void Overlay::edge_indicators(const GLGame *glgame, const GLShip *glship) {
  if (!glship->ship->is_alive()) return;
  if (glgame->objects->empty()) return;

  int nx = glgame->num_x_viewports();
  int ny = glgame->num_y_viewports();

  // Overlay half-extents in orthogonal coordinates
  float hw = (float)Typer::window_width / nx;
  float hh = (float)Typer::window_height / ny;

  // Perspective visible half-extents at z=0, matching draw_perspective
  float fov_deg = (ny == 1) ? glship->view_angle() : glship->view_angle() * 0.75f;
  float half_h = tanf(fov_deg * (float)M_PI / 360.0f) * 1000.0f;
  float aspect = ((float)glgame->window.x() / nx) / ((float)glgame->window.y() / ny);
  float half_w = half_h * aspect;

  float scale_x = hw / half_w;
  float scale_y = hh / half_h;

  // View rotation matching draw_perspective
  float dir_deg = glship->rotate_view() ? glship->camera_facing() : 0.0f;
  float dir_rad = dir_deg * (float)M_PI / 180.0f;
  float cos_d = cosf(dir_rad);
  float sin_d = sinf(dir_rad);

  Point ship_pos = glship->ship->position;

  const float inset = 40.0f;
  const float arrow_size = 24.0f;

  float edge_hw = hw - inset;
  float edge_hh = hh - inset;

  // Only show off-screen arrows when no killable asteroids are visible on screen
  for (auto it = glgame->objects->cbegin(); it != glgame->objects->cend(); ++it) {
    const Asteroid *a = *it;
    if (!a->alive || a->invincible) continue;

    Point closest = a->position.closest_to(ship_pos);
    float wdx = closest.x() - ship_pos.x();
    float wdy = closest.y() - ship_pos.y();

    float sx = (wdx * cos_d - wdy * sin_d) * scale_x;
    float sy = (wdx * sin_d + wdy * cos_d) * scale_y;

    float r_sx = a->effective_radius() * scale_x;
    float r_sy = a->effective_radius() * scale_y;
    if (sx - r_sx < hw && sx + r_sx > -hw && sy - r_sy < hh && sy + r_sy > -hh) return;
  }

  glColor3f(1.0f, 1.0f, 1.0f);

  for (auto it = glgame->objects->cbegin(); it != glgame->objects->cend(); ++it) {
    const Asteroid *a = *it;
    if (!a->alive || a->invincible) continue;

    // Find closest wrapped position to ship
    Point closest = a->position.closest_to(ship_pos);
    float wdx = closest.x() - ship_pos.x();
    float wdy = closest.y() - ship_pos.y();

    // Rotate into screen space
    float sx = (wdx * cos_d - wdy * sin_d) * scale_x;
    float sy = (wdx * sin_d + wdy * cos_d) * scale_y;

    // Skip if asteroid is fully or partially visible
    float r_sx = a->effective_radius() * scale_x;
    float r_sy = a->effective_radius() * scale_y;
    if (sx - r_sx < hw && sx + r_sx > -hw && sy - r_sy < hh && sy + r_sy > -hh) continue;

    // Project direction onto screen edge with inset
    float tx = (fabsf(sx) > 1e-6f) ? edge_hw / fabsf(sx) : 1e9f;
    float ty = (fabsf(sy) > 1e-6f) ? edge_hh / fabsf(sy) : 1e9f;
    float t = (tx < ty) ? tx : ty;

    float ax = fmaxf(fminf(sx * t, edge_hw), -edge_hw);
    float ay = fmaxf(fminf(sy * t, edge_hh), -edge_hh);

    // Arrow angle pointing toward asteroid
    float angle = atan2f(sy, sx);
    float cos_a   = cosf(angle);
    float sin_a   = sinf(angle);
    float cos_a90 = cosf(angle + (float)M_PI / 2.0f);
    float sin_a90 = sinf(angle + (float)M_PI / 2.0f);

    glBegin(GL_TRIANGLES);
    glVertex2f(ax + cos_a * arrow_size,               ay + sin_a * arrow_size);
    glVertex2f(ax + cos_a90 * (arrow_size * 0.5f),    ay + sin_a90 * (arrow_size * 0.5f));
    glVertex2f(ax - cos_a90 * (arrow_size * 0.5f),    ay - sin_a90 * (arrow_size * 0.5f));
    glEnd();
  }
}

void Overlay::paused(const GLGame *glgame, const GLShip *glship) {
  if(!glgame->running && !glship->show_help) {
    Typer::draw_centered(0, 30, "Paused", 25);
    if(is_touch_mode())
      Typer::draw_centered(0, -40, "TOUCH LEVEL TO UNPAUSE", 8);
    else if(glship->has_controller())
      Typer::draw_centered(0, -40, "press start to unpause", 8);
    else
      Typer::draw_centered(0, -40, "press p or esc to unpause", 8);
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

void Overlay::god_mode_indicator(const GLGame *glgame, const GLShip *glship) {
  if(!Asteroid::god_mode) return;
  float vw = Typer::scaled_window_width / glgame->num_x_viewports();
  float vh = Typer::scaled_window_height / glgame->num_y_viewports();
  Typer::draw_centered(0, vh - 50 - CORNER_INSET, "GOD MODE", 12);
}

void Overlay::title_text(const GLGame *glgame, const GLShip *glship) {
  Ship* p1 = glgame->players->front()->ship;
  if(glgame->players->size() < 2) {
    if((glgame->current_time/1400) % 2) {
      if(p1->is_alive() || p1->lives > 0) {
        if(!is_touch_mode()) {
          if(glgame->has_free_controller())
            Typer::draw_centered(Typer::scaled_window_width/2, Typer::scaled_window_height-10, "player 2 press start to join", 8);
          else if(!is_steam_gamemode())
            Typer::draw_centered(Typer::scaled_window_width/2, Typer::scaled_window_height-10, "player 2 press enter to join", 8);
        }
      } else {
        Typer::draw_centered(0, Typer::window_height-10, glship->has_controller() ? "return to menu with start" : "return to menu with ESC", 8);
      }
    }
    if(glship->controller == NULL && !is_touch_mode()) {
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
    if(glship->controller == NULL && !is_touch_mode()) {
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
    const char* unpause = glship->has_controller() ? "press start to unpause" : "press p to unpause";
    Typer::draw_centered(0, Typer::scaled_window_height/glgame->num_y_viewports()-80, unpause, 8);
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
