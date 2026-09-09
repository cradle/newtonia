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
#include <cmath>

TouchControlsState g_touch_controls = {};

// Last real resize, so a mode toggle can re-run the layout in place.
static int s_last_w = 0, s_last_h = 0;

// Held deflection bookkeeping (the one-hand gesture layer below).
static void oh_hold_clear();

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
    float btn_cy   = std::min((float)h * 0.80f, (float)h - 3.6f * btnR);

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

    // Boost: top point of the action diamond, above the shoot/mine pair,
    // at the pair's full size; hit radius capped by the vertical gap to
    // the pair's row so adjacent regions never overlap.
    g_touch_controls.boost_cx     = (shoot_cx + mine_cx) * 0.5f;
    g_touch_controls.boost_cy     = btn_cy - 2.2f * btnR;
    g_touch_controls.boost_radius = btnR;
    g_touch_controls.boost_hit_radius =
        std::min(g_touch_controls.btn_hit_radius, 1.3f * btnR);

    g_touch_controls.teleport_cx = g_touch_controls.boost_cx;
    g_touch_controls.teleport_cy = btn_cy + 2.2f * btnR;
    g_touch_controls.teleport_radius = btnR;
    // Partition adjacent circles at half their centre distance.
    float actionHit = 0.5f * std::hypot(halfGap, 2.2f * btnR);
    g_touch_controls.btn_hit_radius = std::min(g_touch_controls.btn_hit_radius, actionHit);
    g_touch_controls.boost_hit_radius = std::min(g_touch_controls.boost_hit_radius, actionHit);
    g_touch_controls.teleport_hit_radius = std::min(g_touch_controls.boost_hit_radius,
        (float)h - g_touch_controls.teleport_cy);

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
        g_touch_controls.teleport_cx = (float)w - g_touch_controls.teleport_cx;
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
    // ONE HAND: the action arc. SECONDARY / BOOST / TELEPORT HUG the
    // resting ring on its FAR side — the side away from the thumb's
    // pivot — a small gap off the ring's stroke, so each is one short flick
    // from wherever the stick rests and none sits where the stick's own
    // throw begins. Bearings use the math convention (0 = right, +90 =
    // up), and "away" is the real bearing from the pivot to the ring:
    // the bottom bezel's midpoint under CENTRE (straight up), the bottom
    // corner on the thumb's side under LEFT/RIGHT (up and inward — steeper
    // in portrait, where the ring sits higher above that corner). The
    // three spread 60 degrees apart across that far half where the space
    // allows; where a bezel, the zoom column or the pause circle's hit
    // zone would catch an end button, the arc is fitted into the LEGAL
    // span instead — a bearing scan around "away", the spread shrinking
    // (never below 30 degrees) and the apex sliding away from the
    // obstruction — so no button ever shares a finger with the zoom
    // zones (which the gesture layer carves out FIRST). SECONDARY, the
    // busiest, takes the end on the screen-centre side (the short sweep
    // inward), BOOST the apex, TELEPORT the other end; LEFT mirrors that
    // order like the rest of the OSD. The two-hand state fields are
    // reused wholesale — the overlay draw, the gesture layer's hit tests
    // and touch_controls_reset's releases all read them — so the shoot
    // circle is the only two-hand button with no one-hand home (tap-fire
    // is the trigger). Landscape CENTRE is the one honest compromise: the
    // ring's top already sits just under the camera-pinned ship, so the
    // apex circle straddles it there (translucent; LEFT/RIGHT tilt clear).
    // web/main.ts's oneHandArc is the twin — keep the constants in step.
    if (touch_one_handed()) {
        TouchControlsState &tc = g_touch_controls;
        const float DEG = (float)M_PI / 180.0f;
        float R  = tc.joy_radius;
        float cx = tc.joy_hint_cx, cy = tc.joy_hint_cy;
        int side = touch_handedness_side();
        float orbit = R + 0.045f * minDim + btnR;  // gap: a finger's breathing room
        // Hit radius: generous, but tangent to the ring at most — a press
        // that starts inside the resting ring is always the stick.
        float hit = std::min(1.4f * btnR, orbit - R);
        float pivot_x = side == 0 ? cx : side < 0 ? 0.0f : (float)w;
        float away = atan2f((float)h - cy, cx - pivot_x);
        // Keep-outs, pixel space: window bounds (a margin past the
        // radius), the PLACED zoom zones, the pause circle's hit zone.
        const TouchZone zones[2] = { TouchZone::zoom_in_placed(),
                                     TouchZone::zoom_out_placed() };
        float m = btnR + 0.02f * minDim;
        auto legal = [&](float th) {
            float px = cx + orbit * cosf(th), py = cy - orbit * sinf(th);
            if (px < m || px > (float)w - m || py < m || py > (float)h - m)
                return false;
            for (int i = 0; i < 2; i++) {
                float x0 = zones[i].nx0 * (float)w, x1 = zones[i].nx1 * (float)w;
                float y0 = zones[i].ny0 * (float)h, y1 = zones[i].ny1 * (float)h;
                float dx = std::max(std::max(x0 - px, px - x1), 0.0f);
                float dy = std::max(std::max(y0 - py, py - y1), 0.0f);
                if (dx * dx + dy * dy < hit * hit) return false;
            }
            float pdx = px - tc.pause_cx, pdy = py - tc.pause_cy;
            float pr = hit + tc.pause_hit_radius;
            return pdx * pdx + pdy * pdy >= pr * pr;
        };
        // The legal span around "away": the far half plus 15 degrees of
        // slack each side, cut back to the first obstruction either way.
        float lo = away, hi = away;
        while (lo > away - 105.0f * DEG && legal(lo - DEG)) lo -= DEG;
        while (hi < away + 105.0f * DEG && legal(hi + DEG)) hi += DEG;
        float hs = std::max(30.0f * DEG, std::min(60.0f * DEG, (hi - lo) * 0.5f));
        float apex = std::min(std::max(away, lo + hs), hi - hs);
        float sec_off = side < 0 ? -hs : hs;
        float bearing[3] = { apex + sec_off, apex, apex - sec_off };  // sec, boost, tele
        float px[3], py[3];
        for (int i = 0; i < 3; i++) {
            px[i] = cx + orbit * cosf(bearing[i]);
            py[i] = cy - orbit * sinf(bearing[i]);
        }
        tc.mine_cx = px[0];     tc.mine_cy = py[0];     tc.mine_radius = btnR;
        tc.boost_cx = px[1];    tc.boost_cy = py[1];    tc.boost_radius = btnR;
        tc.teleport_cx = px[2]; tc.teleport_cy = py[2]; tc.teleport_radius = btnR;
        tc.btn_hit_radius      = hit;   // the SECONDARY (mine) region
        tc.boost_hit_radius    = hit;
        tc.teleport_hit_radius = hit;
    }

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
    if(g_touch_controls.teleport_pressed) {
        g_touch_controls.teleport_pressed = false;
        game->keyboard_up('t', 0, 0);
    }
    if(g_touch_controls.pause_active) {
        g_touch_controls.pause_active = false;
        game->keyboard_up('p', 0, 0);
    }
    // One-hand gesture leftovers: drop the fire candidate, release a
    // fire-hold's held primary, and release any synthesized fire key
    // still waiting on its deferred up.
    g_touch_controls.oh_tap_active = false;
    // A held deflection driving the ship with no finger down (the
    // joystick branch above only zeroes an ACTIVE stick).
    if(g_touch_controls.oh_hold_engaged) game->touch_joystick(0.0f, 0.0f);
    oh_hold_clear();
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
// Initial lift-to-tap opportunity. Once a tap resumes the input, it stays
// latched until the pilot steers again or the controls reset.
static const Uint32 OH_HOLD_MS = 300;

static float oh_tap_slop() { return g_touch_controls.joy_radius * 0.12f; }

// Discard the lift-off tail without delaying live steering. Keep the most
// recent sample at/before the cutoff and all newer samples; prune on release
// too, since a stationary finger may have sent no recent motion events.
static const Uint32 OH_SAMPLE_MS = 50;
static void oh_prune_samples(Uint32 now) {
    auto &samples = g_touch_controls.oh_stick_samples;
    while (samples.size() > 1 && now - samples[1].ms >= OH_SAMPLE_MS)
        samples.pop_front();
}

static void oh_hold_clear() {
    TouchControlsState &tc = g_touch_controls;
    tc.oh_stick_samples.clear();
    tc.oh_hold_valid   = false;
    tc.oh_hold_engaged = false;
    tc.oh_hold_nx = tc.oh_hold_ny = 0.0f;
    tc.oh_hold_until = 0;
}

// True while a remembered deflection is waiting for a finger: a press
// landing now engages it.
static bool oh_hold_armed(Uint32 now) {
    const TouchControlsState &tc = g_touch_controls;
    return tc.oh_hold_valid && (tc.oh_hold_engaged ||
           (tc.oh_hold_until && (Sint32)(now - tc.oh_hold_until) < 0));
}

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

static bool oh_in_button(float px, float py, float cx, float cy, float hit) {
    float dx = px - cx, dy = py - cy;
    return dx * dx + dy * dy <= hit * hit;
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
    // The action arc (touch_controls_resize): SECONDARY / BOOST / TELEPORT
    // claim their finger ahead of the stick, in live play only — the same
    // gate as tap-fire, so a menu or pause-screen tap on a button's spot
    // stays a plain tap. Press = key down, release = key up, tracked per
    // finger exactly like the two-hand circles, and a finger that slides
    // off keeps its button until it lifts. A press on a cooling-down
    // TELEPORT is claimed too (so it can't fall through and FIRE) but
    // sends nothing — Ship::teleport() would refuse it anyway, and the
    // dimmed circle is the feedback; the key-up on release is inert.
    if (tc.one_hand_ingame) {
        if (tc.mine_available && !tc.mine_pressed &&
            oh_in_button(px, py, tc.mine_cx, tc.mine_cy, tc.btn_hit_radius)) {
            tc.mine_pressed = true;
            tc.mine_finger  = id;
            game->keyboard('x', 0, 0);
            return;
        }
        if (!tc.boost_pressed &&
            oh_in_button(px, py, tc.boost_cx, tc.boost_cy, tc.boost_hit_radius)) {
            // Presses during the cooldown land and no-op in Ship::boost().
            tc.boost_pressed = true;
            tc.boost_finger  = id;
            game->keyboard('e', 0, 0);
            return;
        }
        if (!tc.teleport_pressed &&
            oh_in_button(px, py, tc.teleport_cx, tc.teleport_cy,
                         tc.teleport_hit_radius)) {
            tc.teleport_pressed = true;
            tc.teleport_finger  = id;
            if (tc.teleport_ready) game->keyboard('t', 0, 0);
            return;
        }
    }
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
        tc.oh_stick_samples.clear();
        tc.oh_joy_steered = false;
        tc.oh_joy_fired   = false;
        // Held deflection (touch_controls.h): a press inside the memory
        // window picks the manoeuvre back up — the nub sits at the
        // remembered deflection (the entry point's per-tick apply reads
        // joy_nx/joy_ny, and the OSD draws them) while this finger stays
        // still, and the finger's own wander takes over below in
        // touch_one_hand_motion. A press outside the window is cold: the
        // memory is spent, and the ship stays where the lift left it.
        if (tc.one_hand_ingame && oh_hold_armed(tc.oh_joy_down_ms)) {
            tc.oh_hold_engaged = true;
            tc.oh_hold_until   = 0;  // this finger holds it now
            tc.joy_nx = tc.oh_hold_nx;
            tc.joy_ny = tc.oh_hold_ny;
        } else {
            oh_hold_clear();
        }
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
        // Under an engaged memory the nub is the REMEMBERED deflection
        // until the finger wanders: sub-slop jitter must not overwrite
        // it with a near-centre reading (which would stop the ship the
        // memory exists to keep moving). Past the slop the finger is
        // steering, and the stick is its own again — floating base at
        // the landing point, live deflection from here on.
        // A tap's small wobble must never steer, even without held thrust.
        if (!tc.oh_joy_steered) return;
        tc.oh_hold_engaged = false;
        oh_update_nub(px, py);
        if (tc.oh_joy_steered) {
            Uint32 now = SDL_GetTicks();
            tc.oh_stick_samples.push_back({now, tc.joy_nx, tc.joy_ny});
            oh_prune_samples(now);
        }
    } else if (tc.oh_tap_active && tc.oh_tap_finger == id) {
        if (oh_moved_past_slop(px, py, tc.oh_tap_down_px, tc.oh_tap_down_py))
            tc.oh_tap_steered = true;  // wandered — no shot on release
    }
}

bool touch_one_hand_up(StateManager *game, SDL_FingerID id) {
    TouchControlsState &tc = g_touch_controls;
    Uint32 now = SDL_GetTicks();
    // Action-arc releases first: their fingers were never the stick's.
    // Unconditional on the gate — a button pressed in live play must
    // release even if the game paused under the finger.
    if (tc.mine_pressed && tc.mine_finger == id) {
        tc.mine_pressed = false;
        game->keyboard_up('x', 0, 0);
        return true;
    }
    if (tc.boost_pressed && tc.boost_finger == id) {
        tc.boost_pressed = false;
        game->keyboard_up('e', 0, 0);
        return true;
    }
    if (tc.teleport_pressed && tc.teleport_finger == id) {
        tc.teleport_pressed = false;
        game->keyboard_up('t', 0, 0);
        return true;
    }
    if (tc.joy_active && tc.joy_finger == id) {
        bool tap = !tc.oh_joy_firehold && !tc.oh_joy_steered &&
                   !tc.oh_joy_fired &&
                   now - tc.oh_joy_down_ms < OH_LONG_PRESS_MS;
        // Held deflection (touch_controls.h). An un-wandered release
        // under an engaged memory is a fire gesture's end (a tap, a
        // fire-hold's release, a long press let go): the ship flies on
        // with no timeout. Anything else stops
        // the ship NOW — a stick let go is a stop, whatever follows —
        // and a STEERING release outside the deadzone becomes the memory
        // a tap inside the window picks back up. touch_one_hand_tick
        // applies the remembered deflection while no finger is down.
        bool flies_on = tc.oh_hold_engaged && !tc.oh_joy_steered;
        oh_prune_samples(now);
        float rel_ny = tc.joy_ny;
        float sampled_ny = tc.oh_stick_samples.empty() ? 0.0f :
                           tc.oh_stick_samples.front().ny;
        // Firing keeps thrust, not a held turn: rotation belongs only to
        // the live drag, so repeated taps cannot keep swinging the aim.
        // A centred/reversed thrust axis must not restore its stale sample.
        if (std::fabs(rel_ny) <= 0.10f || rel_ny * sampled_ny <= 0.0f) sampled_ny = 0;
        tc.joy_active = false;
        tc.joy_nx     = 0.0f;
        tc.joy_ny     = 0.0f;
        if (flies_on) {
            tc.oh_hold_until = 0;  // a completed tap keeps the input latched
        } else {
            game->touch_joystick(0.0f, 0.0f);
            if (tc.one_hand_ingame && tc.oh_joy_steered &&
                std::fabs(sampled_ny) > 0.10f) {
                tc.oh_hold_valid   = true;
                tc.oh_hold_engaged = false;
                tc.oh_hold_nx      = 0.0f;
                tc.oh_hold_ny      = sampled_ny;
                tc.oh_hold_until   = now + OH_HOLD_MS;
            } else {
                oh_hold_clear();
            }
        }
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
    if (!tc.one_hand_ingame) {
        // Off the live-play gate (pause, roster, help card, game over, a
        // screen change): a remembered manoeuvre must not outlive it.
        // GLGame::touch_joystick refuses off `running` and the
        // StateManager's forward no-ops off a GLGame, so the zeroing is
        // safe wherever this lands; the pause path's release_controls
        // already stopped the ship, this just keeps the memory honest.
        if (tc.oh_hold_engaged) {
            // A still finger holding an engaged memory carries it in the
            // nub: zero that too, or the resume would fly it again.
            if (tc.joy_active && !tc.oh_joy_steered) {
                tc.joy_nx = 0.0f;
                tc.joy_ny = 0.0f;
            }
            game->touch_joystick(0.0f, 0.0f);
        }
        oh_hold_clear();
        return;
    }
    // A completed tap latches the remembered input until live steering or
    // a reset takes over. Only the initial lift-to-tap opportunity expires.
    if (tc.oh_hold_valid && !tc.joy_active) {
        if (tc.oh_hold_engaged) {
            game->touch_joystick(tc.oh_hold_nx, tc.oh_hold_ny);
        } else if ((Sint32)(now - tc.oh_hold_until) >= 0) {
            oh_hold_clear();
        }
    }
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
