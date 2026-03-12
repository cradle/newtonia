#ifdef __ANDROID__

#include "touch_controls.h"
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
}

#endif // __ANDROID__
