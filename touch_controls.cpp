// Compiled on every platform (see touch_controls.h): pure geometry +
// state, drawn only where touch_osd_enabled().
#include "touch_controls.h"
#include "state_manager.h"
#include "view/overlay.h"
#include <algorithm>

TouchControlsState g_touch_controls = {};

void touch_controls_resize(int w, int h) {
    float minDim = (float)std::min(w, h);
    float ts     = std::min((float)w / 800.0f, (float)h / 600.0f); // matches Typer scale

    // Joystick: bottom-left area
    g_touch_controls.joy_radius   = minDim * 0.20f;
    g_touch_controls.joy_hint_cx  = (float)w * 0.15f;
    g_touch_controls.joy_hint_cy  = (float)h * 0.75f;

    // Action buttons: bottom-right. The natural positions are 0.75/0.90 of the
    // width, but those are width fractions while the radius scales with the
    // SHORTER dimension — so at a narrow portrait width the two buttons bunch
    // together and the mine button ends up jammed against the right bezel.
    // Clamp both: keep the mine button a button-radius of margin off the right
    // edge, and keep a fixed centre-to-centre gap between the two. Both clamps
    // are no-ops at landscape aspect (the width fractions already sit well
    // inside them), so the landscape layout is unchanged.
    float btnR = minDim * 0.07f;

    // Mine (outer, rightmost): never closer to the right edge than 2*btnR from
    // its centre, i.e. a visible ~btnR gap to the bezel.
    float mine_cx = std::min((float)w * 0.90f, (float)w - 2.0f * btnR);
    // Shoot (inner): always at least this far to the LEFT of the mine centre so
    // the two circles keep a visible gap (2.5*btnR centres => ~0.5*btnR apart).
    float shoot_cx = std::min((float)w * 0.75f, mine_cx - 2.5f * btnR);
    float btn_cy   = (float)h * 0.80f;

    g_touch_controls.shoot_cx     = shoot_cx;
    g_touch_controls.shoot_cy     = btn_cy;
    g_touch_controls.shoot_radius = btnR;

    g_touch_controls.mine_cx      = mine_cx;
    g_touch_controls.mine_cy      = btn_cy;
    g_touch_controls.mine_radius  = btnR;

    // Hit radius: half the distance between the two button centres so the touch
    // areas are as large as possible without overlapping each other.
    // Also capped at the distance from the mine centre to the right screen edge.
    float halfGap   = (g_touch_controls.mine_cx - g_touch_controls.shoot_cx) * 0.5f;
    float mineEdge  = (float)w - g_touch_controls.mine_cx;
    g_touch_controls.btn_hit_radius = (halfGap < mineEdge) ? halfGap : mineEdge;

    // Pause button: top-right, below the score AND the multiplier row under
    // it. The HUD stack in Typer units below the top edge (1 unit = ts/2 px;
    // glyphs extend 2*size DOWN from their anchor; everything shifted by the
    // display-cutout inset): score anchored at 75, bottom 115; multiplier
    // "20x" anchored at 135/147, bottom ~177. The fixed 160*ts centre clears
    // that at landscape aspect, but in narrow portrait the radius grows with
    // the WIDTH while the text does not — and a camera-notch inset pushes
    // the text down under the circle — so also require the circle's TOP edge
    // to clear the multiplier bottom (~200 units incl. margin = 100*ts px,
    // plus the inset).
    float pr         = minDim * 0.06f;
    float hud_bottom = 100.0f * ts + Overlay::safe_inset_top();
    g_touch_controls.pause_cx         = (float)w - pr - 0.015f * (float)w;
    g_touch_controls.pause_cy         = std::max(160.0f * ts, hud_bottom + pr);
    g_touch_controls.pause_radius     = pr;
    g_touch_controls.pause_hit_radius = pr * 2.0f;
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
