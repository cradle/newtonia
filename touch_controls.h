#pragma once

// Shared state for the on-screen touch controls. Written by the touch
// entry points (android_main.cpp, ios_main.mm) and read by
// view/overlay.cpp for rendering. Compiled on every platform so the
// screenshot harness can render the touch OSD on desktop; whether the OSD
// actually draws is touch_osd_enabled() below, which keeps every shipped
// build's behaviour exactly as before.

#include <SDL.h>
class StateManager;

// The on-screen joystick/buttons render on the touch platforms; a desktop
// build draws them only under NEWTONIA_FORCE_TOUCH (the screenshot
// harness's mobile-store shots, and the existing touch-layout test hook).
inline bool touch_osd_enabled() {
#if defined(__ANDROID__) || defined(__IOS__)
  return true;
#else
  return SDL_getenv("NEWTONIA_FORCE_TOUCH") != NULL;
#endif
}

struct TouchControlsState {
    // ---- Virtual joystick ----
    // When inactive, draw a faint hint ring at (joy_hint_cx, joy_hint_cy).
    // When active, the base floats to wherever the user first touched on the
    // left half, then the nub tracks within joy_radius pixels.
    float joy_hint_cx, joy_hint_cy; // home position for inactive hint (pixels)
    float joy_radius;               // outer ring radius in pixels
    float joy_cx, joy_cy;           // current base position while active
    float joy_nx, joy_ny;           // normalised nub offset, each in [-1, 1]
    bool  joy_active;
    SDL_FingerID joy_finger;

    // ---- Shoot button ----
    float shoot_cx, shoot_cy, shoot_radius; // centre & radius in pixels
    bool  shoot_pressed;
    SDL_FingerID shoot_finger;

    // ---- Mine button ----
    float mine_cx, mine_cy, mine_radius;
    bool  mine_pressed;
    SDL_FingerID mine_finger;

    // ---- Shared hit-test radius for shoot & mine ----
    // Half the distance between the two button centres so the touch regions are
    // as large as possible without overlapping each other.
    float btn_hit_radius;

    // ---- Pause zone (bottom-centre) ----
    float pause_cx, pause_cy, pause_radius; // visual radius
    float pause_hit_radius;                 // hit-test radius (larger than visual)
    bool  pause_active;
    SDL_FingerID pause_finger;
};

extern TouchControlsState g_touch_controls;

// Call whenever the window is resized to reposition controls.
void touch_controls_resize(int w, int h);

// Release all held touch inputs and send corresponding key-up events.
// Call when the app goes to the background so no inputs get stuck.
void touch_controls_reset(StateManager *game);
