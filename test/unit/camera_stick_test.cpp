// Real GLShip camera/input regression; windowless Linux/GNU ld harness.
#include "glship.h"
#include "grid.h"
#include <cassert>
#include <cmath>
#include <cstdio>

extern "C" bool __wrap__Z12pad_attachedi(int id) { return id == 7 || id == 8; }
extern "C" void __wrap__Z16save_preferencesv() {} // Keep user preferences untouched.

static void axis(GLShip &s, int which, int control, int value) {
  SDL_Event e{};
  e.type = SDL_CONTROLLERAXISMOTION;
  e.caxis.which = which;
  e.caxis.axis = control;
  e.caxis.value = value;
  s.controller_axis_input(e);
}
extern "C" int __wrap_main(int, char **) {
  assert(SDL_Init(SDL_INIT_AUDIO) == 0);
  assert(Mix_OpenAudio(22050, AUDIO_S16SYS, 2, 512) == 0);
  WrappedPoint::set_boundaries(Point(1000, 1000));
  Grid grid(Point(1000, 1000), Point(100, 100));
  GLShip s(grid, true);
  s.set_controller(7);
  bool rotate = false;
  s.set_rotate_view_pref(&rotate);
  float zoom = 1.0f, follow = 0.0f;
  s.set_zoom_prefs(&zoom, &follow);
  axis(s, 8, SDL_CONTROLLER_AXIS_RIGHTY, -32768);
  assert(zoom == 1.0f); // Other seats cannot move this camera.
  axis(s, 7, SDL_CONTROLLER_AXIS_RIGHTY, -32768);
  assert(zoom < 1.0f);
  float first = zoom;
  s.smooth_camera(299);
  assert(zoom == first);
  s.smooth_camera(1);
  assert(zoom < first);
  axis(s, 7, SDL_CONTROLLER_AXIS_RIGHTY, 0);
  first = zoom;
  s.smooth_camera(600);
  assert(zoom == first);
  axis(s, 7, SDL_CONTROLLER_AXIS_RIGHTY, 32767);
  assert(zoom > first);
  for (int i = 0; i < 30; ++i) s.smooth_camera(300);
  assert(zoom == CAMERA_ZOOM_VALUES[CAMERA_ZOOM_STEPS - 1]);
  axis(s, 7, SDL_CONTROLLER_AXIS_RIGHTX, 9000);
  s.smooth_camera(100);
  assert(s.camera_facing() == 0.0f);
  axis(s, 7, SDL_CONTROLLER_AXIS_RIGHTX, 32767);
  s.smooth_camera(100);
  assert(fabsf(s.camera_facing() + 9.0f) < 0.001f);
  axis(s, 7, SDL_CONTROLLER_AXIS_RIGHTX, -32768);
  s.smooth_camera(100);
  assert(fabsf(s.camera_facing()) < 0.001f);
  rotate = true;
  s.set_rotate_view_pref(&rotate);
  first = zoom;
  axis(s, 7, SDL_CONTROLLER_AXIS_RIGHTY, -32768);
  assert(zoom < first); // Zoom remains available in follow mode.
  s.smooth_camera(100);
  rotate = false;
  s.set_rotate_view_pref(&rotate);
  assert(fabsf(s.camera_facing()) < 0.001f); // Fixed angle unaffected in follow mode.
  s.release_controls();
  first = zoom;
  s.smooth_camera(600);
  assert(zoom == first && fabsf(s.camera_facing()) < 0.001f);
  axis(s, 7, SDL_CONTROLLER_AXIS_RIGHTX, 32767);
  s.controller_lost();
  s.smooth_camera(100);
  assert(fabsf(s.camera_facing()) < 0.001f);
  puts("camera_stick_test: PASS");
  return 0;
}
