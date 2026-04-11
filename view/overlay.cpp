#include "overlay.h"
#include "../glship.h"
#include "../glgame.h"
#include "../typer.h"
#include "../ship.h"
#include "../touch_controls.h"

#include "../gl_compat.h"
#include "../mat4.h"
#include "../mesh.h"
#include <cstdio>
#include <cmath>

const float Overlay::CORNER_INSET = 55.0f;

void Overlay::draw(const GLGame *glgame, const GLShip *glship) {
  title_text(glgame, glship);
  level(glgame, glship);
  god_mode(glgame, glship);
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
}

void Overlay::edge_indicators(const GLGame *glgame, const GLShip *glship) {
  if (!glship->ship->is_alive()) return;
  if (glgame->objects->empty()) return;

  int nx = glgame->num_x_viewports();
  int ny = glgame->num_y_viewports();

  // hw_vis: full viewport half-width in ortho coords — used for world→screen scaling
  // and on-screen visibility checks (asteroids visible anywhere on screen are excluded).
  // hw: capped to 16:9 — used only for arrow placement so arrows stay in the OSD zone.
  float hw_vis = (float)Typer::window_width / nx;
  float hw     = Typer::scaled_window_width / nx * Typer::scale;
  float hh = (float)Typer::window_height / ny;

  float fov_deg = (ny == 1) ? glship->view_angle() : glship->view_angle() * 0.75f;
  float half_h = tanf(fov_deg * (float)M_PI / 360.0f) * 1000.0f;
  float aspect = ((float)glgame->window.x() / nx) / ((float)glgame->window.y() / ny);
  float half_w = half_h * aspect;

  float scale_x = hw_vis / half_w;
  float scale_y = hh / half_h;

  float dir_deg = glship->rotate_view() ? glship->camera_facing() : 0.0f;
  float dir_rad = dir_deg * (float)M_PI / 180.0f;
  float cos_d = cosf(dir_rad);
  float sin_d = sinf(dir_rad);

  Point ship_pos = glship->ship->position;

  const float inset      = 40.0f;
  const float arrow_size = 48.0f;
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
    if (sx - r_sx < hw_vis && sx + r_sx > -hw_vis && sy - r_sy < hh && sy + r_sy > -hh) return;
  }

  static MeshBuilder mb;
  static Mesh mesh;
  mb.clear();
  mb.begin(GL_TRIANGLES);
  mb.color(1.0f, 1.0f, 1.0f, 1.0f);

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
    if (sx - r_sx < hw_vis && sx + r_sx > -hw_vis && sy - r_sy < hh && sy + r_sy > -hh) continue;

    float tx = (fabsf(sx) > 1e-6f) ? edge_hw / fabsf(sx) : 1e9f;
    float ty = (fabsf(sy) > 1e-6f) ? edge_hh / fabsf(sy) : 1e9f;
    float t  = (tx < ty) ? tx : ty;
    float ax = fmaxf(fminf(sx * t, edge_hw), -edge_hw);
    float ay = fmaxf(fminf(sy * t, edge_hh), -edge_hh);

    float angle   = atan2f(sy, sx);
    float cos_a   = cosf(angle);
    float sin_a   = sinf(angle);
    float cos_a90 = cosf(angle + (float)M_PI / 2.0f);
    float sin_a90 = sinf(angle + (float)M_PI / 2.0f);

    mb.vertex(ax + cos_a   * arrow_size,            ay + sin_a   * arrow_size);
    mb.vertex(ax + cos_a90 * (arrow_size * 0.5f),   ay + sin_a90 * (arrow_size * 0.5f));
    mb.vertex(ax - cos_a90 * (arrow_size * 0.5f),   ay - sin_a90 * (arrow_size * 0.5f));
  }
  mb.end();
  mesh.upload(mb, GL_DYNAMIC_DRAW);
  mesh.draw();
}

void Overlay::paused(const GLGame *glgame, const GLShip *glship) {
  if(!glgame->running && !glship->show_help) {
    Typer::draw_centered(0, 30, "Paused", 25);
    if(is_touch_mode())
      Typer::draw_centered(0, -40, "press play to resume", 8);
    else if(glship->has_controller())
      Typer::draw_centered(0, -40, "press start to resume", 8);
    else {
      Typer::draw_centered(0, -40, "press p to resume", 8);
      Typer::draw_centered(0, -70, "press esc to return to menu", 8);
    }
  }
}


void Overlay::level(const GLGame *glgame, const GLShip *glship) {
  char buf[20];
  snprintf(buf, sizeof(buf), "LEVEL %d", glgame->generation + 1);
  float vh = Typer::scaled_window_height / glgame->num_y_viewports();
  Typer::draw_centered(0, vh - 20 - CORNER_INSET, buf, 12);
}

void Overlay::god_mode(const GLGame *glgame, const GLShip *glship) {
  int remaining = glship->ship->god_mode_time_remaining();
  if(remaining <= 0) return;
  float vh = Typer::scaled_window_height / glgame->num_y_viewports();
  float base_y = vh - 20 - CORNER_INSET;
  Typer::draw_centered(0, base_y - 62, "God mode", 10);
  Typer::draw_centered(0, base_y - 100, remaining / 1000, 10);
}

void Overlay::score(const GLGame *glgame, const GLShip *glship) {
  float vw = Typer::scaled_window_width / glgame->num_x_viewports();
  float vh = Typer::scaled_window_height / glgame->num_y_viewports();
  Typer::draw(vw - 40 - CORNER_INSET, vh - 20 - CORNER_INSET, glship->ship->score, 20);
  if(glship->ship->multiplier() > 1) {
    Typer::draw(vw - 35 - CORNER_INSET, vh - 92 - CORNER_INSET, "x", 15);
    Typer::draw(vw - 65 - CORNER_INSET, vh - 80 - CORNER_INSET, glship->ship->multiplier(), 20);
  }
}

void Overlay::level_cleared(const GLGame *glgame, const GLShip *glship) {
  if(glgame->running && glgame->level_cleared && (glship->ship->is_alive() || glship->ship->lives > 0)) {
    Typer::draw_centered(0, 150, "CLEARED", 50);
    Typer::draw_centered(0, -60, (glgame->time_until_next_generation / 1000)+1, 20);
  }
}

void Overlay::lives(const GLGame *glgame, const GLShip *glship) {
  Typer::draw_lives(Typer::scaled_window_width/glgame->num_x_viewports()-40-CORNER_INSET, -Typer::scaled_window_height/glgame->num_y_viewports()+70+CORNER_INSET, glship, 18);
}

void Overlay::weapons(const GLGame *glgame, const GLShip *glship) {
  float saved[16]; gles2_get_mvp(saved);
  float s = Typer::scale;
  float vp[16]; mat4_translate(vp, saved,
    (-Typer::scaled_window_width/glgame->num_x_viewports()+CORNER_INSET) * s,
    (Typer::scaled_window_height/glgame->num_y_viewports()-CORNER_INSET) * s, 0.0f);
  gles2_set_vp(vp);
  glship->draw_weapons();
  gles2_set_vp(saved);
}

void Overlay::temperature(const GLGame *glgame, const GLShip *glship) {
  float base[16]; gles2_get_mvp(base);
  float tx = -Typer::scaled_window_width/glgame->num_x_viewports()+30+CORNER_INSET;
  float ty = -Typer::scaled_window_height/glgame->num_y_viewports()+15+CORNER_INSET;
  float inner[16]; mat4_translate(inner, base, tx, ty, 0.0f);
  float temp_vp[16]; mat4_scale(temp_vp, inner, 30.0f, 30.0f, 1.0f);
  gles2_set_vp(temp_vp);
  glship->draw_temperature();
  float status_vp[16]; mat4_translate(status_vp, inner, 42.0f, 147.0f, 0.0f);
  mat4_scale(status_vp, status_vp, 10.0f, 10.0f, 1.0f);
  gles2_set_vp(status_vp);
  glship->draw_temperature_status();
  gles2_set_vp(base);
}

void Overlay::respawn_timer(const GLGame *glgame, const GLShip *glship) {
  if(glgame->running && !glship->show_help) {
    float saved[16]; gles2_get_mvp(saved);
    float vp[16]; mat4_scale(vp, saved, 20.0f, 20.0f, 1.0f);
    gles2_set_vp(vp);
    glship->draw_respawn_timer();
    gles2_set_vp(saved);
  }
}

void Overlay::keymap(const GLGame *glgame, const GLShip *glship) {
  if(glship->show_help) {
    glship->draw_keymap();
  }
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
    const char* unpause = glship->has_controller() ? "press start to resume" : "press p to resume";
    Typer::draw_centered(0, Typer::scaled_window_height/glgame->num_y_viewports()-80, unpause, 8);
  }
}

void Overlay::draw_circle(float cx, float cy, float r, int segs, bool filled,
                          float cr, float cg, float cb, float ca) {
  static MeshBuilder mb;
  static Mesh mesh;
  mb.clear();
  mb.color(cr, cg, cb, ca);
  if (filled) {
    mb.begin(GL_TRIANGLE_FAN);
    mb.vertex(cx, cy);
  } else {
    mb.begin(GL_LINE_LOOP);
  }
  for (int i = 0; i <= segs; i++) {
    float angle = 2.0f * (float)M_PI * (float)i / (float)segs;
    mb.vertex(cx + r * cosf(angle), cy + r * sinf(angle));
  }
  mb.end();
  mesh.upload(mb, GL_DYNAMIC_DRAW);
  mesh.draw();
}

void Overlay::touch_controls(const GLGame *glgame, const GLShip *glship) {
#if defined(__ANDROID__) || defined(__IOS__)
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
    float bx = ox(tc.joy_cx);
    float by = oy(tc.joy_cy);
    draw_circle(bx, by, jr, 32, false, 0.5f, 0.65f, 1.0f, 0.75f);
    float nx_off =  tc.joy_nx * jr;
    float ny_off = -tc.joy_ny * jr;
    draw_circle(bx + nx_off, by + ny_off, jr * 0.38f, 32, true, 0.7f, 0.85f, 1.0f, 0.90f);
  } else {
    draw_circle(jox, joy, jr, 32, false, 0.4f, 0.55f, 1.0f, 0.55f);
    draw_circle(jox, joy, jr * 0.25f, 20, true, 0.4f, 0.55f, 1.0f, 0.40f);
  }

  // ---- Shoot button ----
  {
    float bx = ox(tc.shoot_cx);
    float by = oy(tc.shoot_cy);
    float br = sr(tc.shoot_radius);
    float alpha_fill    = tc.shoot_pressed ? 0.55f : 0.25f;
    float alpha_outline = tc.shoot_pressed ? 0.95f : 0.70f;
    draw_circle(bx, by, br, 28, true,  1.0f, 0.35f, 0.35f, alpha_fill);
    draw_circle(bx, by, br, 28, false, 1.0f, 0.35f, 0.35f, alpha_outline);
  }

  // ---- Mine button ----
  {
    float bx = ox(tc.mine_cx);
    float by = oy(tc.mine_cy);
    float br = sr(tc.mine_radius);
    float alpha_fill    = tc.mine_pressed ? 0.55f : 0.25f;
    float alpha_outline = tc.mine_pressed ? 0.95f : 0.70f;
    draw_circle(bx, by, br, 28, true,  0.35f, 0.6f, 1.0f, alpha_fill);
    draw_circle(bx, by, br, 28, false, 0.35f, 0.6f, 1.0f, alpha_outline);
  }

  // ---- Pause button ----
  {
    float bx = ox(tc.pause_cx);
    float by = oy(tc.pause_cy);
    float br = sr(tc.pause_radius);
    float alpha_fill    = tc.pause_active ? 0.35f : 0.08f;
    float alpha_outline = tc.pause_active ? 0.70f : 0.30f;
    draw_circle(bx, by, br, 32, true,  1.0f, 1.0f, 1.0f, alpha_fill);
    draw_circle(bx, by, br, 32, false, 1.0f, 1.0f, 1.0f, alpha_outline);

    static MeshBuilder mb;
    static Mesh mesh_icon;
    mb.clear();
    if (glgame->running) {
      // Two vertical bars (pause icon) — GL_QUADS replaced with GL_TRIANGLES
      float bw  = br * 0.15f;
      float bh  = br * 0.38f;
      float sep = br * 0.20f;
      mb.begin(GL_TRIANGLES);
      mb.color(1.0f, 1.0f, 1.0f, alpha_outline);
      // Left bar
      mb.vertex(bx - sep - bw*2, by - bh);
      mb.vertex(bx - sep,        by - bh);
      mb.vertex(bx - sep,        by + bh);
      mb.vertex(bx - sep - bw*2, by - bh);
      mb.vertex(bx - sep,        by + bh);
      mb.vertex(bx - sep - bw*2, by + bh);
      // Right bar
      mb.vertex(bx + sep,        by - bh);
      mb.vertex(bx + sep + bw*2, by - bh);
      mb.vertex(bx + sep + bw*2, by + bh);
      mb.vertex(bx + sep,        by - bh);
      mb.vertex(bx + sep + bw*2, by + bh);
      mb.vertex(bx + sep,        by + bh);
      mb.end();
    } else {
      // Right-pointing triangle (play/resume icon)
      float th = br * 0.45f;
      float tx = bx - br * 0.05f;
      mb.begin(GL_TRIANGLES);
      mb.color(1.0f, 1.0f, 1.0f, alpha_outline);
      mb.vertex(tx - th * 0.6f, by - th);
      mb.vertex(tx + th,        by);
      mb.vertex(tx - th * 0.6f, by + th);
      mb.end();
    }
    mesh_icon.upload(mb, GL_DYNAMIC_DRAW);
    mesh_icon.draw();
  }
#endif // __ANDROID__ || __IOS__
}
