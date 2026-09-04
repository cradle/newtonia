#include "intro.h"
#include "glgame.h"
#include "glenemy.h"
#include "menu.h"
#include "asset_path.h"
#include "audio_volume.h"
#include "preferences.h"
#include "asteroid.h"
#include "asteroid_drawer.h"
#include "wrapped_point.h"
#include "view/overlay.h"
#include "touch_controls.h"
#include "typer.h"

#include "gl_compat.h"
#include "mat4.h"

#include <iostream>
#include <list>

Intro::Intro(GLGame *game, Kind kind, const char *name,
             std::vector<Asteroid *> display_asteroids, int hazard_kind) :
  State(),
  game(game),
  kind(kind),
  name(name),
  asteroids_(std::move(display_asteroids)),
  hazard_kind(hazard_kind) {
  for (Asteroid *a : asteroids_) {
    // Park the display asteroids at the world centre so the fixed intro
    // camera (which looks at the focus point) keeps them on screen while
    // they spin; draw() spreads a multi-rock row out from here once the
    // window aspect is known.
    a->position = WrappedPoint(game->world.x() / 2.0f, game->world.y() / 2.0f);
    a->velocity = Point(0, 0);
  }
  if (kind == INTERCEPTOR || kind == BOMBER || kind == RAMMER) {
    // Display-only hull at the world centre (see the member note in
    // intro.h). An empty target list keeps its Follower inert; the Ship
    // constructor starts the looping engine hum, so silence it like the
    // stations do.
    std::list<GLShip *> no_targets;
    display_enemy_ = new GLEnemy(game->grid,
                                 game->world.x() / 2.0f,
                                 game->world.y() / 2.0f,
                                 &no_targets, 0.0f, NULL, 0.0f,
                                 kind == BOMBER ? GLEnemy::BOMBER
                                 : kind == RAMMER ? GLEnemy::RAMMER
                                                  : GLEnemy::INTERCEPTOR);
    // A Ship constructs dead and only comes alive through step()'s respawn
    // (never run here — the world is frozen) or a direct raise, which is
    // exactly how the station deploys its wave ships. GLShip::draw skips
    // the hull of a dead ship, so without this the intro showed an empty
    // starfield with a name under it.
    display_enemy_->ship->alive = true;
    // mute_engine() is protected (the stations reach it by inheriting
    // Ship); zeroing the scale and re-leveling the looping hum channel is
    // the public equivalent for the intro's 5 seconds.
    display_enemy_->ship->sound_volume_scale = 0.0f;
    display_enemy_->ship->update_boost_volume();
    // Thrusting pose: the hull holds still (never stepped, so the thrust
    // force moves nothing) while tick() animates the exhaust trail behind
    // it. Silenced above, so thrust(true)'s hum re-level stays at zero.
    // Nose to the right: the default nose-up pose streamed the plume down
    // through the name caption below. A cruise velocity makes it FLY:
    // tick() integrates it with Object::step (position only — never
    // Ship::step, see the intro.h note), the camera focus tracks the hull,
    // and the parallax starfield streaming past sells the motion.
    display_enemy_->ship->facing = Point(1.0f, 0.0f);
    display_enemy_->ship->velocity = Point(kind == BOMBER ? 0.12f : 0.3f, 0.0f);  // rammer cruises like the interceptor
    display_enemy_->ship->thrust(true);
  }
  // Silence looping effects (e.g. the respawn shield hum) while the intro is
  // up; the world is frozen so their sources are frozen too. The intro tune
  // starts after the pause so its own channel keeps playing.
  Mix_Pause(-1);
  music_sound = Mix_LoadWAV(asset_path("audio/intro.wav").c_str());
  if (music_sound == NULL) {
    std::cout << "Unable to load intro.wav (" << Mix_GetError() << ")" << std::endl;
  } else {
    // The tune follows the MUSIC volume (audio_volume.h; master reaches it
    // through the channel master volume like every channel). Chunk-level,
    // not channel-level: the chunk is this state's own, so it can't
    // retro-level anything else, and a lowered dynamic CHANNEL would leak
    // its volume to whoever is allocated that channel next.
    Mix_VolumeChunk(music_sound,
                    (int)(MIX_MAX_VOLUME * AudioVolume::music_scale() + 0.5f));
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
  for (Asteroid *a : asteroids_) {
    delete a;
  }
  delete display_enemy_;
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
  camera_delta_pending_ += delta;
  time += delta;
  if (time >= auto_start_ms) {
    dismiss();
    return;
  }
  step_accum += delta;
  while (step_accum >= GLGame::step_size) {
    for (Asteroid *a : asteroids_) a->step(GLGame::step_size);
    // The display interceptor cruises (position integration only — the
    // qualified call must never reach Ship::step, see the intro.h note)
    // while its exhaust animates; the camera tracks it via focus().
    if ((kind == INTERCEPTOR || kind == BOMBER || kind == RAMMER) && display_enemy_ != NULL) {
      display_enemy_->ship->Object::step(GLGame::step_size);
      display_enemy_->step_trails(GLGame::step_size);
    }
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
    case INTERCEPTOR:
    case BOMBER:
    case RAMMER:       return display_enemy_->ship->position;
    case HAZARD: {
      Hazard *h = game->first_hazard((Hazard::Kind)hazard_kind);
      if (h != NULL) return h->position;
      return Point(game->world.x() / 2.0f, game->world.y() / 2.0f);
    }
    case ASTEROID:
    default:
      // The display row is centred on the world centre (single rock: parked
      // exactly there), so the camera looks at the centre either way.
      return Point(game->world.x() / 2.0f, game->world.y() / 2.0f);
  }
}

void Intro::draw() {
  // Keep the game's camera smoothing ticking while the intro holds the world:
  // banked from this state's own tick, the same way GLGame::draw takes it from
  // GLGame::tick (a wall clock here would hand the first frame after dismissal
  // however long the player left the intro up).
  for (GLShip *gs : *game->players) gs->smooth_camera(camera_delta_pending_);
  camera_delta_pending_ = 0;

  glClear(GL_COLOR_BUFFER_BIT);
  glViewport(0, 0, window.x(), window.y());

  Point focus_point = focus();
  // Player 1's live FOV (the zoom prefs fold into view_angle()), not a
  // fixed 85: the frames on either side of this screen are drawn at the
  // zoomed view, and a hardcoded default made the starfield jump ~20% on
  // open and dismissal at CLOSEST/WIDEST. Full-window, so no split factor.
  float fov_deg = game->players->empty() ? 85.0f
                                         : game->players->front()->view_angle();
  float proj[16]; mat4_perspective(proj, fov_deg, window.x() / (float)window.y(), 100.0f, 2000.0f);
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

  // Spread a multi-rock row (the TOUGH ELITES intro) around the focus.
  // Done here, not the ctor: the spacing follows the window aspect so the
  // row fits portrait phones, and `window` is only set once the state is
  // installed. Re-done every frame, which also tracks live resizes; the
  // rocks' own step() never moves them (velocity zero), only tumbles them.
  if (kind == ASTEROID && asteroids_.size() > 1) {
    float aspect = window.y() > 0.0f ? window.x() / window.y() : 1.6f;
    float spread = 820.0f * (aspect < 1.6f ? aspect / 1.6f : 1.0f);
    for (size_t i = 0; i < asteroids_.size(); i++)
      asteroids_[i]->position = WrappedPoint(
          game->world.x() / 2.0f + ((float)i - (asteroids_.size() - 1) / 2.0f) * spread,
          game->world.y() / 2.0f);
  }

  bool invisible_intro = (kind == ASTEROID && asteroids_.size() == 1 &&
                          asteroids_[0]->invisible);
  if (invisible_intro) {
    AsteroidDrawer::draw_invisible_mask(asteroids_[0], focus_point.x(), focus_point.y());
    game->starfield->draw_stars_near(focus_point.x(), focus_point.y(), asteroids_[0]->radius);
  }

  switch (kind) {
    case ASTEROID: {
      list<Asteroid*> row, none;
      for (Asteroid *a : asteroids_) row.push_back(a);
      AsteroidDrawer::draw_batch(&row, &none, 0.0f, false);
      break;
    }
    case BLACK_HOLE:   game->black_holes->front()->draw(false); break;
    case MINI_STATION: game->mini_station->draw(false);         break;
    case STATION:      game->station->draw(false);              break;
    case INTERCEPTOR:
    case BOMBER:
    case RAMMER:       display_enemy_->draw(false);             break;
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
    game->starfield->draw_front_stars_near(focus_point.x(), focus_point.y(), asteroids_[0]->radius);
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    game->warp_pass_->capture(vp[0], vp[1], vp[2], vp[3]);
    game->warp_pass_->draw(asteroids_[0], focus_point.x(), focus_point.y(), vp[0], vp[1], vp[2], vp[3]);
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
    // One-handed touch has no fire button — a tap anywhere IS the fire
    // input there (the gesture layer's stale-true gate, touch_controls.h),
    // so the prompt says what the finger actually does.
    Typer::draw_centered(0, top * 0.75f,
                         is_touch_mode()
                             ? (touch_one_handed() ? "TAP TO START"
                                                   : "TAP FIRE TO START")
                             : "PRESS FIRE TO START", 20);
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
  if (!paused && time >= input_delay_ms) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
      if (g_prefs.player_keys[i].shoot.matches(key)) {
        dismiss();
        break;
      }
    }
  }
}

void Intro::keyboard_up(unsigned char key, int x, int y) {
  // Only the menu and skip-level keys act while the intro is up; shoot (on
  // key down) starts.
  if (is_finished()) return;
  if (key == (unsigned char)g_prefs.general_keys.menu) {
    leave_to_menu();
    return;
  }
  // Skip-level during an intro skips this level too: apply the game's own
  // skip handling (the intro is for the level being skipped), then start.
  // One N per level whether an intro is up or not — level-marching driver
  // scripts and the debug key behave identically on intro generations.
  if (key == (unsigned char)g_prefs.general_keys.skip_level) {
    game->keyboard_up(key, x, y);
    // The game's skip zeroes Asteroid::num_killable assuming every live
    // asteroid was in its lists — but our display copy is still alive and
    // its destructor decrements on teardown, leaving the count at -1.
    // Every branch of the level-clear ladder is gated on num_killable == 0,
    // so the skipped level then never rolled over: an empty world stuck on
    // the CLEARED banner (asteroid intros only — the black hole / station
    // intros have no display copies). Pre-count each copy so its destructor
    // nets to zero.
    for (Asteroid *a : asteroids_)
      if (!a->invincible)
        Asteroid::num_killable++;
    dismiss();
  }
}

void Intro::controller(SDL_Event event) {
  if (is_finished()) return;
  // Start (or Guide) pauses, matching the in-game pause button. Start must
  // be claimed BEFORE the shared translation below folds it into confirm.
  if (event.type == SDL_CONTROLLERBUTTONDOWN &&
      (event.cbutton.button == SDL_CONTROLLER_BUTTON_START ||
       event.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE)) {
    toggle_pause();
    return;
  }
  // Shared nav language: A / right trigger = confirm (start the level),
  // B / Back = Esc (leave to the menu, auto-saving — the keyboard's menu
  // key already did this; a pad previously had no way out of an intro).
  unsigned char k = nav_key_from_controller(event);
  if (k == 27) {
    leave_to_menu();
    return;
  }
  if (k == '\r' && !paused && time >= input_delay_ms) {
    dismiss();
  }
}

void Intro::touch_tap(float nx, float ny) {
  // Desktop clicks (Steam Deck touchscreen taps arrive as clicks — see
  // glut.cpp mouse()) dismiss like a fire press, under the same gates.
  // Touch platforms keep the fire-button-only rule (the OSD fire button
  // synthesizes the shoot key into keyboard()); a tap anywhere stays inert
  // there so the intro still teaches where the fire button is.
  if (is_touch_mode()) return;
  if (is_finished() || paused || time < input_delay_ms) return;
  dismiss();
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
