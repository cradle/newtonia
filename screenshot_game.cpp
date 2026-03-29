#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)

#include "screenshot_game.h"
#include "asteroid.h"
#include "wrapped_point.h"
#include <cmath>
#include <cstdlib>

// Safe-area computation for a 3840x1240 window with an 860x380 centre safe zone.
//
// Camera: z=1000, vertical FOV=85°
//   half_h = tan(42.5°) × 1000 ≈ 916.3 world units  (maps to 620 px)
//   aspect  = 3840 / 1240 ≈ 3.0968
//   half_w  = half_h × aspect ≈ 2837.9 world units  (maps to 1920 px)
//
// Safe zone half-extents in world units:
//   safe_hw = (860/2) / 1920 × 2837.9 ≈ 635.7
//   safe_hh = (380/2) /  620 ×  916.3 ≈ 280.8
static const float SAFE_HW = 636.0f;
static const float SAFE_HH = 281.0f;

static const float WORLD_W = 2500.0f;
static const float WORLD_H = 2500.0f;
// Camera centres on world mid-point so safe area is screen-centre.
static const float CAM_X   = WORLD_W / 2.0f;   // 1250
static const float CAM_Y   = WORLD_H / 2.0f;   // 1250

ScreenshotGame::ScreenshotGame() : GLGame() {
  // 1. Move camera (player ship) to world centre.
  set_camera_position(CAM_X, CAM_Y);

  // 2. Clear the 3 default asteroids spawned by GLGame().
  while(!objects->empty()) {
    delete objects->back();
    objects->pop_back();
  }
  while(!dead_objects->empty()) {
    delete dead_objects->back();
    dead_objects->pop_back();
  }
  Asteroid::num_killable = 0;

  // 3. Spawn asteroids with rejection sampling for the safe zone.
  //    Mix: 20 normal  (killable)  + 20 invincible.
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

  for(int i = 0; i < 20; i++) place(false);
  for(int i = 0; i < 20; i++) place(true);

  // 4. Freeze physics and disable HUD / ship rendering.
  screenshot_mode_ = true;
}

#endif // !__ANDROID__ && !__EMSCRIPTEN__
