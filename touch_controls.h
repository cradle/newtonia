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
    // The mine button only exists while the local ship has a secondary
    // equipped (secondaries come from pickups and drop off the ship when
    // the last one runs dry). GLGame::tick writes this each frame; the
    // overlay draw and the entry points' hit tests both read it, so the
    // region can never answer a finger the circle isn't showing. Zero-init
    // false: in the menus nothing updates it, and the 'x' key the region
    // would send is inert there anyway.
    bool  mine_available;

    // ---- Boost button ----
    // Above the shoot/mine pair (boost discoverability: touch had NO boost
    // control at all — the late-game design rule has kept every escape
    // possible on plain thrust, but the mechanic itself deserved a button).
    // Always available, like shoot. Sends P1's boost key ('e'), the same
    // hard-coded-default convention as shoot/mine/pause.
    float boost_cx, boost_cy, boost_radius;
    float boost_hit_radius;  // capped below the vertical gap to the pair
    bool  boost_pressed;
    // Ship::boost_ready() mirrored each frame by GLGame::tick (the
    // mine_available pattern): the overlay dims the button while the
    // cooldown runs. Presses during cooldown still land and no-op in
    // Ship::boost() — the flag is presentation, not a gate.
    bool  boost_ready;
    SDL_FingerID boost_finger;

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
