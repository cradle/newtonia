#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)

#include "screenshot_game.h"
#include "asteroid.h"
#include "wrapped_point.h"
#include <cmath>
#include <cstdlib>

// World dimensions for the screenshot:
//   Width 5700 matches the visible world width so asteroids never repeat
//   horizontally across the 3840x1240 view.
//
// Visible half-extents at z=1000, vertical FOV=85°, aspect=3840/1240:
//   half_h = tan(42.5°) × 1000 ≈ 916.3  →  full visible height ≈ 1832  (<2500, no y-looping)
//   half_w = half_h × 3.0968     ≈ 2837.9 →  full visible width  ≈ 5676  (≤5700, no x-looping)
static const float WORLD_W = 5700.0f;
static const float WORLD_H = 2500.0f;

// Camera centres on world mid-point so safe area is screen-centre.
static const float CAM_X = WORLD_W / 2.0f;   // 2850
static const float CAM_Y = WORLD_H / 2.0f;   // 1250

// Safe-area half-extents in world units (860x380 px safe zone):
//   safe_hw = (860/2) / 1920 × 2837.9 ≈ 635.7
//   safe_hh = (380/2) /  620 ×  916.3 ≈ 280.8
static const float SAFE_HW = 636.0f;
static const float SAFE_HH = 281.0f;

ScreenshotGame::ScreenshotGame() : GLGame() {
  // 0. Freeze immediately so no tick/controller path can add a second player.
  screenshot_mode_ = true;

  // 1. Widen the world to match the view so asteroids don't loop.
  resize_world(WORLD_W, WORLD_H);

  // 2. Move camera (player ship) to world centre.
  set_camera_position(CAM_X, CAM_Y);

  // 3. Clear the default asteroids spawned by GLGame().
  while(!objects->empty()) {
    delete objects->back();
    objects->pop_back();
  }
  while(!dead_objects->empty()) {
    delete dead_objects->back();
    dead_objects->pop_back();
  }
  Asteroid::num_killable = 0;

  // 4. Spawn asteroids with rejection sampling for the safe zone.
  //    80 normal (killable) + 80 invincible = 160 total.
  auto place = [&](bool invincible) {
    for(int attempt = 0; attempt < 200; attempt++) {
      Asteroid *a = new Asteroid(invincible);

      // Wrapped distance from camera position.
      float dx = a->position.x() - CAM_X;
      while(dx >  WORLD_W / 2.0f) dx -= WORLD_W;
      while(dx < -WORLD_W / 2.0f) dx += WORLD_W;
      float dy = a->position.y() - CAM_Y;
      while(dy >  WORLD_H / 2.0f) dy -= WORLD_H;
      while(dy < -WORLD_H / 2.0f) dy += WORLD_H;

      // Expand safe zone by the asteroid's own radius so it doesn't clip in.
      float margin = a->radius + 20.0f;
      if(fabsf(dx) < SAFE_HW + margin && fabsf(dy) < SAFE_HH + margin) {
        delete a;
        continue;
      }
      objects->push_back(a);
      return;
    }
    // Fallback (very unlikely): place without safe-zone constraint.
    objects->push_back(new Asteroid(invincible));
  };

  for(int i = 0; i < 80; i++) place(false);
  for(int i = 0; i < 80; i++) place(true);

  // (screenshot_mode_ already set at top of constructor)
}

#endif // !__ANDROID__ && !__EMSCRIPTEN__
