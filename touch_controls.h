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
    // Active-weapon icons: the shoot/mine circles carry a glyph naming the
    // selected primary/secondary (Save::WeaponEntry::Kind values, mirrored
    // each frame by GLGame::tick like mine_available; secondary_kind is
    // meaningful only while mine_available). The web build receives the
    // same pair over the setWeaponKinds bridge.
    unsigned char primary_kind;
    unsigned char secondary_kind;

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

    // ---- One-handed mode (Preferences::touch_one_hand) ----
    // The whole screen is one joystick resting centred just below the
    // ship, and the OSD draws no shoot/mine/boost circles. A press that never wanders
    // past the tap slop is a FIRE gesture instead of steering: released
    // before the long-press threshold it taps the primary (' '), held past
    // it the secondary fires ('x', gated on mine_available exactly like
    // the mine button it replaces). GLGame::tick mirrors one_hand_ingame
    // from touch_zoom_active() — live play with a local ship — so menu,
    // pause-screen, replay and game-over taps never synthesize fire keys
    // (the flag deliberately goes stale-true under the Intro state, where
    // a tap-anywhere IS the fire input that dismisses it); ~GLGame clears
    // it, since ' ' doubles as a confirm key in the menus. The synthesized
    // press is released a beat later by touch_one_hand_tick, never in the
    // same event batch — the weapons only sample the trigger in step().
    bool  one_hand_ingame;
    // One-hand shield toggle (see oh_long_press_secondary): the shield is
    // the one hold-to-run secondary — active while the key is down,
    // draining as it renews — which the pulse below would blink on for a
    // single beat. Under the one-hand grammar a long press TOGGLES it
    // instead, and the decision keys on THIS mirror — the SELECTED
    // secondary's own trigger, (*secondary)->is_shooting() when it is the
    // shield, written by GLGame::tick beside secondary_kind — never a
    // local latch: a respawn's trigger reset, an ammo-out or a level
    // rollover simply reads back as "off" and the next long press
    // re-engages, nothing to desync or clean up.
    bool  shield_engaged;
    // The joystick finger doubles as the first fire candidate.
    Uint32 oh_joy_down_ms;
    float oh_joy_down_px, oh_joy_down_py;
    bool  oh_joy_steered;   // wandered past the slop: it is steering
    bool  oh_joy_fired;     // its long-press secondary already fired
    // A second finger while the first steers: a pure fire candidate that
    // never steals the stick (the two-hand layout let a second left-half
    // finger re-base the joystick mid-flight).
    bool  oh_tap_active;
    SDL_FingerID oh_tap_finger;
    Uint32 oh_tap_down_ms;
    float oh_tap_down_px, oh_tap_down_py;
    bool  oh_tap_steered;
    bool  oh_tap_fired;
    // Deferred key-ups for the synthesized fire presses (0 = none armed).
    Uint32 oh_shoot_up_at;
    Uint32 oh_mine_up_at;
    // ---- Double-tap-hold: sustained primary fire ----
    // A press landing within OH_DOUBLE_TAP_MS of the last tap-fire is
    // unambiguously SHOOTING, so it becomes a FIRE-HOLD: the synthesized
    // ' ' goes down at the press edge and stays down until the finger
    // lifts — automatics stream, and the joystick finger still steers
    // (the fire-hold never cancels on movement). Rapid tap-spam trips
    // this naturally and degrades gracefully: a quickly-released
    // fire-hold is just a tap-length pull. Releasing refreshes the tap
    // chain, so tap-hold-tap-hold stays in the stream. A fire-hold press
    // never long-presses — the secondary needs a COLD press, one that
    // follows no recent tap (pause a beat, then hold).
    Uint32 oh_last_tap_ms;  // when the last tap fired / fire-hold released
    bool  oh_joy_firehold;
    bool  oh_tap_firehold;
};

extern TouchControlsState g_touch_controls;

// Call whenever the window is resized to reposition controls.
void touch_controls_resize(int w, int h);

// Re-run the last resize (options toggled the input method: the joystick
// hint/radius move without the window changing). No-op before the first
// real resize.
void touch_controls_relayout();

// Release all held touch inputs and send corresponding key-up events.
// Call when the app goes to the background so no inputs get stuck.
void touch_controls_reset(StateManager *game);

// ---- One-handed input (Preferences::touch_one_hand) ----
// The shared gesture layer for the native entry points: when
// touch_one_handed() is on, finger_down/motion/up delegate here wholesale
// (only the top-right pause button keeps its own hit test — the invisible
// centre pause zone is the joystick field now), and the per-tick loop
// calls touch_one_hand_tick beside the joystick apply for the long-press
// watchdog and the deferred fire-key releases. The web build's HTML OSD
// implements the same gesture in web/main.ts.
bool touch_one_handed();
// Handedness (Preferences::touch_handedness): -1 LEFT, 0 CENTRE, +1
// RIGHT. In ONE HAND mode LEFT/RIGHT rest the ring where that thumb
// sits; CENTRE keeps it centred (the two-hand layout ignores the
// RIGHT/CENTRE distinction — its classic arrangement IS the right-handed
// one).
int touch_handedness_side();
// True under HANDEDNESS LEFT: the OSD mirrors to put the busy controls
// under the left thumb. In ONE HAND that moves the remaining inputs —
// the pause circle and the zoom column; in TWO HANDS the WHOLE layout
// flips: the stick claims the right half, the shoot/mine/boost circles
// the left, pause and zoom crossing with them. touch_controls_resize
// mirrors the geometry (so every centre-based hit test follows for
// free), TouchZone::zoom_*_placed() mirrors the zoom zones, and the
// entry points' half splits + the web build's hard-coded zones key on
// this predicate directly.
bool touch_layout_mirrored();
// px/py = window pixels, nx/ny = the normalized 0..1 SDL finger coords
// (the zoom-zone carve-out speaks normalized, like touch_tap).
void touch_one_hand_down(StateManager *game, SDL_FingerID id,
                         float px, float py, float nx, float ny);
void touch_one_hand_motion(SDL_FingerID id, float px, float py);
// Returns false for a finger it never tracked (zoom-zone or overflow):
// the caller falls through to its legacy '\r' release.
bool touch_one_hand_up(StateManager *game, SDL_FingerID id);
void touch_one_hand_tick(StateManager *game);
