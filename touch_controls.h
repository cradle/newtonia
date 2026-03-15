#pragma once

// Shared state for the on-screen touch controls (Android only).
// Included by android_main.cpp (writes state) and view/overlay.cpp (reads for
// rendering).  All code is guarded by __ANDROID__ so desktop builds are
// unaffected.

#if defined(__ANDROID__) || defined(__IOS__)

#include <SDL.h>
class StateManager;

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

    // ---- Pause zone (top-centre, over the LEVEL text) ----
    float pause_cx, pause_cy, pause_radius;
    bool  pause_active;
    SDL_FingerID pause_finger;
};

extern TouchControlsState g_touch_controls;

// Call whenever the window is resized to reposition controls.
void touch_controls_resize(int w, int h);

// Release all held touch inputs and send corresponding key-up events.
// Call when the app goes to the background so no inputs get stuck.
void touch_controls_reset(StateManager *game);

#endif // __ANDROID__ || __IOS__
