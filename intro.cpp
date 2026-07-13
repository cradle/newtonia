#include "intro.h"
#include "glgame.h"
#include "menu.h"
#include "asset_path.h"
#include "preferences.h"
#include "asteroid.h"
#include "asteroid_drawer.h"
#include "wrapped_point.h"
#include "view/overlay.h"
#include "typer.h"

#include "gl_compat.h"
#include "mat4.h"

#include <iostream>
#include <list>

Intro::Intro(GLGame *game, Kind kind, const char *name, Asteroid *display_asteroid,
             int hazard_kind) :
  State(),
  game(game),
  kind(kind),
  name(name),
  asteroid(display_asteroid),
  hazard_kind(hazard_kind) {
  if (asteroid != NULL) {
    // Park the display asteroid at the world centre so the fixed intro camera
    // (which looks at the focus point) keeps it centre-screen while it spins.
    asteroid->position = WrappedPoint(game->world.x() / 2.0f, game->world.y() / 2.0f);
    asteroid->velocity = Point(0, 0);
  }
  // Silence looping effects (e.g. the respawn shield hum) while the intro is
  // up; the world is frozen so their sources are frozen too. The intro tune
  // starts after the pause so its own channel keeps playing.
  Mix_Pause(-1);
  music_sound = Mix_LoadWAV(asset_path("audio/intro.wav").c_str());
  if (music_sound == NULL) {
    std::cout << "Unable to load intro.wav (" << Mix_GetError() << ")" << std::endl;
  } else {
    music_channel = Mix_PlayChannel(-1, music_sound, -1);
  }
}

Intro::~Intro() {
  if (music_channel >= 0) {
    Mix_HaltChannel(music_channel);
  }
  if (music_sound != NULL) {
    Mix_FreeChunk(music_sound);
  }
  if (asteroid != NULL) {
    delete asteroid;
  }
  // Still owning the game means we left to the menu (or shut down) without
  // handing it back; deleting it saves progress.
  if (!handed_back && game != NULL) {
    delete game;
  }
}

void Intro::dismiss() {
  if (music_channel >= 0) {
    Mix_HaltChannel(music_channel);
    music_channel = -1;
  }
  Mix_Resume(-1);
  // This state swallowed all ship input while it was up: a thrust/stick
  // release during the intro never reached the ships, and a centred stick
  // sends no further events — release everything before play resumes or a
  // control held over the level transition stays latched ON.
  game->release_player_controls();
  // Ownership of the game returns to the StateManager; play resumes exactly
  // where it froze. `game` must stay set (not owned): this state remains
  // current — and still draws — until the manager performs the swap.
  handed_back = true;
  request_state_change(game);
}

void Intro::leave_to_menu() {
  if (music_channel >= 0) {
    Mix_HaltChannel(music_channel);
    music_channel = -1;
  }
  Mix_Resume(-1);
  // `game` stays owned: ~Intro deletes it, which saves progress.
  request_state_change(new Menu());
}

void Intro::toggle_pause() {
  paused = !paused;
  // Freeze/thaw the intro tune alongside the auto-start countdown so a paused
  // intro is silent, matching the in-game pause. (While unfocused the channel
  // is already paused; focus_gained() won't un-pause it if we stay paused.)
  if (music_channel >= 0) {
    if (paused) Mix_Pause(music_channel);
    else if (!unfocused) Mix_Resume(music_channel);
  }
}

void Intro::tick(int delta) {
  if (is_finished() || unfocused || paused) return;
  time += delta;
  if (time >= auto_start_ms) {
    dismiss();
    return;
  }
  step_accum += delta;
  while (step_accum >= GLGame::step_size) {
    if (asteroid != NULL) asteroid->step(GLGame::step_size);
    if (kind == BLACK_HOLE) {
      for (auto bhi = game->black_holes->begin(); bhi != game->black_holes->end(); bhi++)
        (*bhi)->step(GLGame::step_size);
    }
    if (kind == HAZARD) {
      // Animate the focused hazard (pulsar shockwave, seeker blink); the fixed
      // intro camera tracks its position, so any drift stays centre-screen.
      Hazard *h = game->first_hazard((Hazard::Kind)hazard_kind);
      if (h != NULL) h->update(GLGame::step_size, game->players);
    }
    step_accum -= GLGame::step_size;
  }
}

Point Intro::focus() const {
  switch (kind) {
    case BLACK_HOLE:   return game->black_holes->front()->position;
    case MINI_STATION: return game->mini_station->position;
    case STATION:      return game->station->position;
    case HAZARD: {
      Hazard *h = game->first_hazard((Hazard::Kind)hazard_kind);
      if (h != NULL) return h->position;
      return Point(game->world.x() / 2.0f, game->world.y() / 2.0f);
    }
    case ASTEROID:
    default:           return asteroid->position;
  }
}

void Intro::draw() {
  // Keep the game's camera smoothing and draw-timing baseline ticking so the
  // first frame after dismissal doesn't see a multi-second frame delta.
  Uint32 now = SDL_GetTicks();
  int frame_delta = (int)(now - game->last_draw_time_);
  game->last_draw_time_ = now;
  for (GLShip *gs : *game->players) gs->smooth_camera(frame_delta);

  glClear(GL_COLOR_BUFFER_BIT);
  glViewport(0, 0, window.x(), window.y());

  Point focus_point = focus();
  float proj[16]; mat4_perspective(proj, 85.0f, window.x() / (float)window.y(), 100.0f, 2000.0f);
  float view[16]; mat4_lookat(view, 0.0f, 0.0f, 1000.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
  float pv[16]; mat4_mul(pv, proj, view);

  // Starfield backdrop, tiled 3x3 around the focus point like
  // GLGame::draw_perspective (the focus object can sit near a world edge).
  for (int x = -1; x <= 1; x++) {
    for (int y = -1; y <= 1; y++) {
      float tile_vp[16];
      mat4_translate(tile_vp, pv, game->world.x()*x - focus_point.x(),
                                  game->world.y()*y - focus_point.y(), 0.0f);
      gles2_set_vp(tile_vp);
      game->starfield->draw_rear(focus_point);
    }
  }

  float center_vp[16];
  mat4_translate(center_vp, pv, -focus_point.x(), -focus_point.y(), 0.0f);
  gles2_set_vp(center_vp);

  bool invisible_intro = (kind == ASTEROID && asteroid->invisible);
  if (invisible_intro) {
    AsteroidDrawer::draw_invisible_mask(asteroid, focus_point.x(), focus_point.y());
    game->starfield->draw_stars_near(focus_point.x(), focus_point.y(), asteroid->radius);
  }

  switch (kind) {
    case ASTEROID: {
      list<Asteroid*> one, none;
      one.push_back(asteroid);
      AsteroidDrawer::draw_batch(&one, &none, 0.0f, false);
      break;
    }
    case BLACK_HOLE:   game->black_holes->front()->draw(false); break;
    case MINI_STATION: game->mini_station->draw(false);         break;
    case STATION:      game->station->draw(false);              break;
    case HAZARD: {
      Hazard *h = game->first_hazard((Hazard::Kind)hazard_kind);
      if (h != NULL) h->draw(false);
      break;
    }
  }

  for (int x = -1; x <= 1; x++) {
    for (int y = -1; y <= 1; y++) {
      float tile_vp[16];
      mat4_translate(tile_vp, pv, game->world.x()*x - focus_point.x(),
                                  game->world.y()*y - focus_point.y(), 0.0f);
      gles2_set_vp(tile_vp);
      game->starfield->draw_front(focus_point);
    }
  }

  if (invisible_intro) {
    gles2_set_vp(center_vp);
    game->starfield->draw_front_stars_near(focus_point.x(), focus_point.y(), asteroid->radius);
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    game->warp_pass_->capture(vp[0], vp[1], vp[2], vp[3]);
    game->warp_pass_->draw(asteroid, focus_point.x(), focus_point.y(), vp[0], vp[1], vp[2], vp[3]);
  }

  // Text overlay: flashing prompt at the top, object name below the object.
  float hw = window.x() / Overlay::SAFE_AREA_SCALE;
  float hh = window.y() / Overlay::SAFE_AREA_SCALE;
  float ortho[16];
  mat4_ortho(ortho, -hw, hw, -hh, hh, -1.0f, 1.0f);
  gles2_set_vp(ortho);
  // Touch OSD stays visible so the player can find the fire button that
  // dismisses the intro (no-op off Android/iOS).
  if (!game->players->empty())
    Overlay::touch_controls(game, game->players->front());
  // Typer multiplies coordinates by Typer::scale (800x600 virtual space), so
  // convert the ortho half-height into Typer units to place text on screen.
  float top = hh / Typer::scale;
  if (paused) {
    // Countdown (and thus `time`) is frozen while paused, so show a steady
    // PAUSED in place of the flashing start prompt.
    Typer::draw_centered(0, top * 0.75f, "PAUSED", 20);
  } else if ((time / 700) % 2 == 0) {
    Typer::draw_centered(0, top * 0.75f,
                         is_touch_mode() ? "TAP FIRE TO START" : "PRESS FIRE TO START", 20);
  }
  Typer::draw_centered(0, -top * 0.5f, name, 26);
}

void Intro::keyboard(unsigned char key, int x, int y) {
  if (is_finished()) return;
  // Pause freezes the auto-start countdown (and the intro tune) so the intro
  // holds indefinitely; press again to resume.
  if (key == (unsigned char)g_prefs.general_keys.pause) {
    toggle_pause();
    return;
  }
  // Shoot dismisses, but not while paused (as in-game, shots don't fire when
  // paused). Small delay so a shoot key held over the level transition doesn't
  // dismiss the intro before it is seen. The touch fire button arrives here
  // too (it synthesises the shoot key).
  if (!paused && time >= input_delay_ms &&
      (key == (unsigned char)g_prefs.p1_keys.shoot ||
       key == (unsigned char)g_prefs.p2_keys.shoot)) {
    dismiss();
  }
}

void Intro::keyboard_up(unsigned char key, int x, int y) {
  // Only the menu key acts while the intro is up; shoot (on key down) starts.
  if (!is_finished() && key == (unsigned char)g_prefs.general_keys.menu) {
    leave_to_menu();
  }
}

void Intro::controller(SDL_Event event) {
  if (is_finished()) return;
  // Start (or Guide) pauses, matching the in-game pause button.
  if (event.type == SDL_CONTROLLERBUTTONDOWN &&
      (event.cbutton.button == SDL_CONTROLLER_BUTTON_START ||
       event.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE)) {
    toggle_pause();
    return;
  }
  if (!paused && time >= input_delay_ms &&
      ((event.type == SDL_CONTROLLERBUTTONDOWN &&
        event.cbutton.button == SDL_CONTROLLER_BUTTON_A) ||
       (event.type == SDL_CONTROLLERAXISMOTION &&
        event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT &&
        event.caxis.value > 8000))) {
    dismiss();
  }
}

bool Intro::back_pressed() {
  if (!is_finished()) leave_to_menu();
  return true;
}

void Intro::focus_lost() {
  unfocused = true;
  if (music_channel >= 0) Mix_Pause(music_channel);
}

void Intro::focus_gained() {
  unfocused = false;
  // Stay silent if the player paused the intro before losing focus.
  if (music_channel >= 0 && !paused) Mix_Resume(music_channel);
}

void Intro::controller_added(SDL_GameController *ctrl) {
  if (game != NULL) game->controller_added(ctrl);
}

void Intro::controller_removed(SDL_JoystickID id) {
  if (game != NULL) game->controller_removed(id);
}
