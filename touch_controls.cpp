// Compiled on every platform (see touch_controls.h): pure geometry +
// state, drawn only where touch_osd_enabled().
#include "touch_controls.h"
#include "preferences.h"
#include "savegame.h"
#include "state_manager.h"
#include "view/overlay.h"
#include "view/tap_band.h"
#include <algorithm>
#include <math.h>

TouchControlsState g_touch_controls = {};

// Last real resize, so a mode toggle can re-run the layout in place.
static int s_last_w = 0, s_last_h = 0;

bool touch_one_handed() { return g_prefs.touch_one_hand; }

int touch_handedness_side() {
    return g_prefs.touch_handedness - 1;  // stored 0/1/2 -> -1/0/+1
}

bool touch_layout_mirrored() { return touch_handedness_side() < 0; }

void touch_controls_resize(int w, int h) {
    s_last_w = w;
    s_last_h = h;
    float minDim = (float)std::min(w, h);
    float ts     = std::min((float)w / 800.0f, (float)h / 600.0f); // matches Typer scale

    if (touch_one_handed()) {
        // One hand: the whole screen is the stick, so the resting hint is
        // a touch larger than the two-hand ring (0.30 was field-rejected
        // as too big). Horizontally it follows HANDEDNESS: CENTRE keeps
        // it centred; LEFT/RIGHT rest it where that thumb naturally sits,
        // the ring's near edge a small margin off its bezel (in 16:9
        // landscape that lands near 0.15w — the same neighbourhood as the
        // two-hand stick's home). Vertically it rests at the LOWER of two
        // anchors: the midpoint between the screen centre and the bottom
        // (0.75h — where the thumb rests in portrait, field request
        // 2026-09-03), floored by the below-the-ship anchor — the camera
        // pins the ship to the viewport centre, so the ring's TOP edge
        // must clear h/2 by a margin, and in landscape (minDim IS the
        // height) that anchor (0.5 + 0.05 + 0.22 = 0.77h) is the one that
        // binds, with 0.77 + 0.22 < 1 keeping the whole ring on screen.
        g_touch_controls.joy_radius   = minDim * 0.22f;
        float side_cx = minDim * 0.05f + g_touch_controls.joy_radius;
        int side = touch_handedness_side();
        g_touch_controls.joy_hint_cx  =
            side == 0 ? (float)w * 0.5f
                      : side < 0 ? side_cx : (float)w - side_cx;
        g_touch_controls.joy_hint_cy  =
            std::max((float)h * 0.5f + minDim * 0.05f +
                         g_touch_controls.joy_radius,
                     (float)h * 0.75f);
    } else {
        // Joystick: bottom-left area
        g_touch_controls.joy_radius   = minDim * 0.20f;
        g_touch_controls.joy_hint_cx  = (float)w * 0.15f;
        g_touch_controls.joy_hint_cy  = (float)h * 0.75f;
    }

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

    // Boost: above and between the shoot/mine pair — the thumb triangle,
    // at the pair's full size; hit radius capped by the vertical gap to
    // the pair's row so the three regions never overlap.
    g_touch_controls.boost_cx     = (shoot_cx + mine_cx) * 0.5f;
    g_touch_controls.boost_cy     = btn_cy - 2.2f * btnR;
    g_touch_controls.boost_radius = btnR;
    g_touch_controls.boost_hit_radius =
        std::min(g_touch_controls.btn_hit_radius, 1.3f * btnR);

    // HANDEDNESS LEFT mirrors the whole TWO-HAND layout: the stick's home
    // crosses to the right, the shoot/mine/boost circles to the left.
    // Mirroring the COMPUTED centres (rather than computing left-side
    // variants) keeps every clamp and radius above correct by symmetry —
    // btn_hit_radius's bezel cap measures the same distance to the left
    // edge that it measured to the right — and the entry points' hit
    // tests read these centres, so only their joystick half split needs
    // its own flip. One-hand placement chose its own side above.
    if (!touch_one_handed() && touch_layout_mirrored()) {
        g_touch_controls.joy_hint_cx = (float)w - g_touch_controls.joy_hint_cx;
        g_touch_controls.shoot_cx    = (float)w - g_touch_controls.shoot_cx;
        g_touch_controls.mine_cx     = (float)w - g_touch_controls.mine_cx;
        g_touch_controls.boost_cx    = (float)w - g_touch_controls.boost_cx;
    }

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

    // HANDEDNESS LEFT crosses the pause circle to the top-LEFT in both
    // input methods (the zoom column mirrors with it through
    // TouchZone::zoom_*_placed). One geometry drives the draw and every
    // entry point's hit test, so the flip here moves both. It may brush
    // a long WEAPONS list — cosmetic: the circle is translucent and the
    // list is not a tap target.
    if (touch_layout_mirrored())
        g_touch_controls.pause_cx = (float)w - g_touch_controls.pause_cx;
}

void touch_controls_relayout() {
    if (s_last_w > 0 && s_last_h > 0) touch_controls_resize(s_last_w, s_last_h);
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
    if(g_touch_controls.boost_pressed) {
        g_touch_controls.boost_pressed = false;
        game->keyboard_up('e', 0, 0);
    }
    if(g_touch_controls.pause_active) {
        g_touch_controls.pause_active = false;
        game->keyboard_up('p', 0, 0);
    }
    // One-hand gesture leftovers: drop the fire candidate, release a
    // fire-hold's held primary, and release any synthesized fire key
    // still waiting on its deferred up.
    g_touch_controls.oh_tap_active = false;
    if(g_touch_controls.oh_joy_firehold || g_touch_controls.oh_tap_firehold) {
        g_touch_controls.oh_joy_firehold = false;
        g_touch_controls.oh_tap_firehold = false;
        game->keyboard_up(' ', 0, 0);
    }
    if(g_touch_controls.oh_shoot_up_at) {
        g_touch_controls.oh_shoot_up_at = 0;
        game->keyboard_up(' ', 0, 0);
    }
    if(g_touch_controls.oh_mine_up_at) {
        g_touch_controls.oh_mine_up_at = 0;
        game->keyboard_up('x', 0, 0);
    }
}

// ============================================================
// One-handed gesture layer (see touch_controls.h)
// ============================================================

// Gesture thresholds. LONG_PRESS balances "deliberate hold" against a
// secondary arriving mid-dodge; the tap slop is the wander budget — past
// it the press is steering and can never fire (a fraction of the larger
// one-hand joystick radius: ~3.5% of the short screen edge, a few mm).
static const Uint32 OH_LONG_PRESS_MS = 400;
// How long a synthesized fire key stays down before touch_one_hand_tick
// releases it. The weapons only sample the trigger in their step(), so a
// down+up in the same event batch would fire nothing at all.
static const Uint32 OH_KEY_HOLD_MS = 70;
// A press starting this close behind a tap-fire becomes a FIRE-HOLD
// (touch_controls.h): comfortably above a spam-tap gap, comfortably
// below a deliberate pause-then-long-press for the secondary.
static const Uint32 OH_DOUBLE_TAP_MS = 250;

static float oh_tap_slop() { return g_touch_controls.joy_radius * 0.12f; }

static bool oh_moved_past_slop(float px, float py, float ox, float oy) {
    float dx = px - ox, dy = py - oy;
    float slop = oh_tap_slop();
    return dx * dx + dy * dy > slop * slop;
}

// Same nub math as the entry points' two-hand update_joystick_nub.
static void oh_update_nub(float px, float py) {
    float dx = px - g_touch_controls.joy_cx;
    float dy = py - g_touch_controls.joy_cy;
    float dist = sqrtf(dx * dx + dy * dy);
    float r = g_touch_controls.joy_radius;
    if (dist > r) {
        dx = dx * r / dist;
        dy = dy * r / dist;
    }
    g_touch_controls.joy_nx = (r > 0.0f) ? dx / r : 0.0f;
    g_touch_controls.joy_ny = (r > 0.0f) ? dy / r : 0.0f;
}

static void oh_fire_primary(StateManager *game) {
    game->keyboard(' ', 0, 0);
    g_touch_controls.oh_shoot_up_at = SDL_GetTicks() + OH_KEY_HOLD_MS;
    // Opens the double-tap window: the next quick press streams.
    g_touch_controls.oh_last_tap_ms = SDL_GetTicks();
}

// True (and the primary pressed AND HELD) when this press lands inside
// the double-tap window — see the fire-hold note in touch_controls.h.
static bool oh_start_firehold(StateManager *game) {
    TouchControlsState &tc = g_touch_controls;
    Uint32 now = SDL_GetTicks();
    if (!tc.one_hand_ingame || !tc.oh_last_tap_ms ||
        now - tc.oh_last_tap_ms > OH_DOUBLE_TAP_MS)
        return false;
    // The hold owns the key now: a tap's still-pending deferred release
    // must not cut the stream short a beat after it starts.
    tc.oh_shoot_up_at = 0;
    game->keyboard(' ', 0, 0);
    return true;
}

static void oh_fire_secondary(StateManager *game) {
    game->keyboard('x', 0, 0);
    g_touch_controls.oh_mine_up_at = SDL_GetTicks() + OH_KEY_HOLD_MS;
}

// The long-press action. Most secondaries fire on the press edge, so the
// synthesized 'x' is a pulse (down + deferred release). The SHIELD is the
// exception — hold-to-run, which a pulse would blink on for one beat — so
// a long press TOGGLES it: engage = press 'x' and simply never schedule
// the release ('x' stays held, exactly a desktop pilot's held key, so the
// weapon/netplay/replay layers see nothing new); disengage = release it.
// The on/off decision reads the mirrored trigger truth (shield_engaged,
// touch_controls.h), so a state the engine reset on its own — respawn,
// rollover — just reads as "off" again.
static void oh_long_press_secondary(StateManager *game) {
    TouchControlsState &tc = g_touch_controls;
    if (tc.secondary_kind == (unsigned char)Save::WeaponEntry::Kind::Shield) {
        if (tc.shield_engaged)
            game->keyboard_up('x', 0, 0);
        else
            game->keyboard('x', 0, 0);
        return;
    }
    oh_fire_secondary(game);
}

void touch_one_hand_down(StateManager *game, SDL_FingerID id,
                         float px, float py, float nx, float ny) {
    TouchControlsState &tc = g_touch_controls;
    // In live play the zoom zones keep their tap semantics: a finger
    // starting there stays untracked so its release reaches touch_tap as
    // a plain tap (the zoom step) instead of firing the gun. The PLACED
    // zones, not the statics — LEFT handedness mirrors the column.
    if (tc.one_hand_ingame &&
        (TouchZone::zoom_in_placed().contains(nx, ny) ||
         TouchZone::zoom_out_placed().contains(nx, ny)))
        return;
    if (!tc.joy_active) {
        // First finger: the joystick, floating base exactly like the
        // two-hand layout — the mid-screen ring is a resting hint, not an
        // anchor. Deflection measured from wherever the finger lands is
        // what keeps a still press still, which is what makes tap vs
        // long-press vs steering decidable at all (a centre-anchored
        // stick would read every off-centre touch as full deflection).
        tc.joy_cx     = px;
        tc.joy_cy     = py;
        tc.joy_nx     = 0.0f;
        tc.joy_ny     = 0.0f;
        tc.joy_active = true;
        tc.joy_finger = id;
        tc.oh_joy_down_ms = SDL_GetTicks();
        tc.oh_joy_down_px = px;
        tc.oh_joy_down_py = py;
        tc.oh_joy_steered = false;
        tc.oh_joy_fired   = false;
        // Double-tap window: this press is a fire-hold — the primary is
        // already down and stays down until the finger lifts, while the
        // deflection below still steers.
        tc.oh_joy_firehold = oh_start_firehold(game);
        // '\r' is ignored during gameplay but lets any tap start from the
        // menu — the same pairing the two-hand left half sends.
        game->keyboard('\r', 0, 0);
    } else if (!tc.oh_tap_active) {
        // Second finger while the first steers: a pure fire candidate.
        tc.oh_tap_active  = true;
        tc.oh_tap_finger  = id;
        tc.oh_tap_down_ms = SDL_GetTicks();
        tc.oh_tap_down_px = px;
        tc.oh_tap_down_py = py;
        tc.oh_tap_steered = false;
        tc.oh_tap_fired   = false;
        tc.oh_tap_firehold = oh_start_firehold(game);
    }
    // Further fingers are silently ignored.
}

void touch_one_hand_motion(SDL_FingerID id, float px, float py) {
    TouchControlsState &tc = g_touch_controls;
    if (tc.joy_active && tc.joy_finger == id) {
        // The steered latch is one-way: once a press has been a dodge it
        // can never late-fire by drifting back over its start point.
        if (oh_moved_past_slop(px, py, tc.oh_joy_down_px, tc.oh_joy_down_py))
            tc.oh_joy_steered = true;
        oh_update_nub(px, py);
    } else if (tc.oh_tap_active && tc.oh_tap_finger == id) {
        if (oh_moved_past_slop(px, py, tc.oh_tap_down_px, tc.oh_tap_down_py))
            tc.oh_tap_steered = true;  // wandered — no shot on release
    }
}

bool touch_one_hand_up(StateManager *game, SDL_FingerID id) {
    TouchControlsState &tc = g_touch_controls;
    Uint32 now = SDL_GetTicks();
    if (tc.joy_active && tc.joy_finger == id) {
        bool tap = !tc.oh_joy_firehold && !tc.oh_joy_steered &&
                   !tc.oh_joy_fired &&
                   now - tc.oh_joy_down_ms < OH_LONG_PRESS_MS;
        tc.joy_active = false;
        tc.joy_nx     = 0.0f;
        tc.joy_ny     = 0.0f;
        game->touch_joystick(0.0f, 0.0f);
        // Pair the '\r' sent in touch_one_hand_down
        game->keyboard_up('\r', 0, 0);
        if (tc.oh_joy_firehold) {
            tc.oh_joy_firehold = false;
            game->keyboard_up(' ', 0, 0);
            // Refresh the chain: a quick re-press continues the stream.
            tc.oh_last_tap_ms = now;
        } else if (tap && tc.one_hand_ingame) {
            oh_fire_primary(game);
        }
        return true;
    }
    if (tc.oh_tap_active && tc.oh_tap_finger == id) {
        bool tap = !tc.oh_tap_firehold && !tc.oh_tap_steered &&
                   !tc.oh_tap_fired &&
                   now - tc.oh_tap_down_ms < OH_LONG_PRESS_MS;
        tc.oh_tap_active = false;
        if (tc.oh_tap_firehold) {
            tc.oh_tap_firehold = false;
            game->keyboard_up(' ', 0, 0);
            tc.oh_last_tap_ms = now;
        } else if (tap && tc.one_hand_ingame) {
            oh_fire_primary(game);
        }
        return true;
    }
    return false;  // untracked finger: caller's legacy '\r' release path
}

void touch_one_hand_tick(StateManager *game) {
    TouchControlsState &tc = g_touch_controls;
    Uint32 now = SDL_GetTicks();
    // Deferred fire-key releases (see OH_KEY_HOLD_MS above).
    if (tc.oh_shoot_up_at && (Sint32)(now - tc.oh_shoot_up_at) >= 0) {
        tc.oh_shoot_up_at = 0;
        game->keyboard_up(' ', 0, 0);
    }
    if (tc.oh_mine_up_at && (Sint32)(now - tc.oh_mine_up_at) >= 0) {
        tc.oh_mine_up_at = 0;
        game->keyboard_up('x', 0, 0);
    }
    if (!tc.one_hand_ingame) return;
    // Long-press watchdog: a held, un-wandered press fires the secondary
    // once, while the finger is still down (motion events stop when the
    // finger stops, so release-time checks alone would fire it late).
    if (tc.joy_active && !tc.oh_joy_firehold && !tc.oh_joy_steered &&
        !tc.oh_joy_fired && tc.mine_available &&
        now - tc.oh_joy_down_ms >= OH_LONG_PRESS_MS) {
        tc.oh_joy_fired = true;
        oh_long_press_secondary(game);
    }
    if (tc.oh_tap_active && !tc.oh_tap_firehold && !tc.oh_tap_steered &&
        !tc.oh_tap_fired && tc.mine_available &&
        now - tc.oh_tap_down_ms >= OH_LONG_PRESS_MS) {
        tc.oh_tap_fired = true;
        oh_long_press_secondary(game);
    }
}
