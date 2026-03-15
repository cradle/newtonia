#if defined(__ANDROID__) || defined(__IOS__)

#include "touch_controls.h"
#include "state_manager.h"
#include <algorithm>

TouchControlsState g_touch_controls = {};

void touch_controls_resize(int w, int h) {
    float minDim = (float)std::min(w, h);

    // Joystick: bottom-left area
    g_touch_controls.joy_radius   = minDim * 0.10f;
    g_touch_controls.joy_hint_cx  = (float)w * 0.15f;
    g_touch_controls.joy_hint_cy  = (float)h * 0.80f;

    // Action buttons: bottom-right area
    float btnR = minDim * 0.07f;

    g_touch_controls.shoot_cx     = (float)w * 0.75f;
    g_touch_controls.shoot_cy     = (float)h * 0.82f;
    g_touch_controls.shoot_radius = btnR;

    g_touch_controls.mine_cx      = (float)w * 0.90f;
    g_touch_controls.mine_cy      = (float)h * 0.82f;
    g_touch_controls.mine_radius  = btnR;

    // Hit radius: half the distance between the two button centres so the touch
    // areas are as large as possible without overlapping each other.
    // Also capped at the distance from the mine centre to the right screen edge.
    float halfGap   = (g_touch_controls.mine_cx - g_touch_controls.shoot_cx) * 0.5f;
    float mineEdge  = (float)w - g_touch_controls.mine_cx;
    g_touch_controls.btn_hit_radius = (halfGap < mineEdge) ? halfGap : mineEdge;

    // Pause zone: top-centre over the LEVEL text
    g_touch_controls.pause_cx     = (float)w * 0.5f;
    g_touch_controls.pause_cy     = (float)h * 0.07f;
    g_touch_controls.pause_radius = minDim * 0.10f;
}

void touch_controls_reset(StateManager *game) {
    if(g_touch_controls.joy_active) {
        g_touch_controls.joy_active = false;
        g_touch_controls.joy_nx = 0.0f;
        g_touch_controls.joy_ny = 0.0f;
        game->touch_joystick(0.0f, 0.0f);
        game->keyboard_up('\r', 0, 0);
    }
    if(g_touch_controls.shoot_pressed) {
        g_touch_controls.shoot_pressed = false;
        game->keyboard_up(' ', 0, 0);
    }
    if(g_touch_controls.mine_pressed) {
        g_touch_controls.mine_pressed = false;
        game->keyboard_up('x', 0, 0);
    }
    if(g_touch_controls.pause_active) {
        g_touch_controls.pause_active = false;
        game->keyboard_up('p', 0, 0);
    }
}

#endif // __ANDROID__ || __IOS__
