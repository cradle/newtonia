// iOS entry point using SDL2.
// Replaces glut.cpp on iOS: handles window creation, the game loop,
// touch → keyboard/controller event mapping, and audio initialisation.
// Mirrors android_main.cpp closely; SDL2 handles the UIKit lifecycle.

#ifdef __IOS__

#include <SDL.h>
#include <SDL_mixer.h>

#include "achievements.h"
#include "gles2_compat.h"
#include "net_signal.h"
#include "net_transport.h"
#include "state_manager.h"
#include "touch_controls.h"
#include "typer.h"
#include "asteroid.h"
#include "preferences.h"
#include "world_sound.h"
#include "view/overlay.h"

#include <iostream>
#include <cmath>
#include <cstdlib>
#include <ctime>

// ============================================================
// Touch → game-input mapping
// ============================================================
// The screen is divided into:
//   LEFT HALF  (x < 0.5)  – virtual joystick (floating base)
//   RIGHT HALF (x >= 0.5) – action buttons or legacy keys
//
// Right half layout:
//   top strip (y < 0.4)           → '\r'  (start / confirm for menu)
//   bottom-left button area       → ' '   (shoot)
//   bottom-right button area      → 'x'   (mine)
//
// Any tap anywhere also sends '\r' down/up so that tapping anywhere on the
// screen starts the game from the menu (Menu::keyboard_up handles any key).
//
// Button hit-testing uses the positions configured by touch_controls_resize.
// Hit-test radius is btn_hit_radius: half the distance between button centres.
// DEBUG: very top-right corner (x>0.85, y<0.15) → 'n' (skip level).

static StateManager *s_game          = nullptr;
static SDL_Window   *s_window        = nullptr;
static SDL_GLContext s_gl_ctx        = nullptr;
static int           s_w = 800, s_h = 600;
static bool          s_running       = true;
static bool          s_reset_tick    = false;  // set on focus-gained to skip catch-up

// ---- Utility ----

// Display safe insets: ios_safe_area.mm reads UIKit's safeAreaInsets (a
// Point-free TU — this file cannot import UIKit). Forwarded to the HUD so
// the top-anchored row (LEVEL/score/weapons) clears the camera notch in
// portrait, exactly as android_main.cpp does with DisplayCutout. Called at
// startup and again on every SDL resize, which is what rotation delivers.
//
// UIKit reports insets in logical POINTS; Overlay wants physical pixels
// (what Android hands it), and s_w/s_h are the HiDPI drawable's pixels —
// so scale by the drawable/points ratio SDL knows.
extern "C" void ios_safe_area_insets(float *top, float *bottom,
                                     float *left, float *right);

static void read_display_safe_insets() {
    float top = 0.0f, bottom = 0.0f, left = 0.0f, right = 0.0f;
    ios_safe_area_insets(&top, &bottom, &left, &right);

    int pw = 0, ph = 0;
    SDL_GetWindowSize(s_window, &pw, &ph);   // points
    float scale = (pw > 0) ? (float)s_w / (float)pw : 1.0f;
    top *= scale; bottom *= scale; left *= scale; right *= scale;

    Overlay::set_safe_insets(top, bottom, left, right);
    SDL_Log("Safe insets: top=%d bottom=%d left=%d right=%d",
            (int)top, (int)bottom, (int)left, (int)right);
}

// Rotation (and any other window-size change). SDL's UIKit backend reports
// it from -viewDidLayoutSubviews as a plain window resize, so this is the
// ONLY notification the game gets — and s_w/s_h also feed the finger_*
// normalized->pixel maps, so without it the viewport, text scale, and every
// touch zone would stay in the old orientation's geometry.
//
// The event payload is in logical POINTS (the view's bounds), not the HiDPI
// drawable pixels the rest of this file works in, so the new size is
// re-queried rather than taken from data1/data2 — the one place iOS differs
// from android_main.cpp, which has no points/pixels split.
static void apply_window_size() {
    if (!s_game || !s_window) return;

    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(s_window, &w, &h);
    if (w <= 0 || h <= 0 || (w == s_w && h == s_h)) return;

    touch_controls_reset(s_game);   // held touches: their zones just moved
    s_w = w;
    s_h = h;
    s_game->resize(s_w, s_h);
    Typer::resize(s_w, s_h);
    // Insets before the touch layout — the pause button clears the
    // inset-shifted HUD row, and the insets rotate with the display (a
    // portrait top notch becomes a landscape side one).
    read_display_safe_insets();
    touch_controls_resize(s_w, s_h);
}

static inline float tc_dist(float ax, float ay, float bx, float by) {
    float dx = ax - bx, dy = ay - by;
    return sqrtf(dx * dx + dy * dy);
}

// Update the joystick nub position from an absolute screen-pixel position.
static void update_joystick_nub(float px, float py) {
    float dx = px - g_touch_controls.joy_cx;
    float dy = py - g_touch_controls.joy_cy;
    float dist = sqrtf(dx * dx + dy * dy);
    float r = g_touch_controls.joy_radius;
    if(dist > r) {
        dx = dx * r / dist;
        dy = dy * r / dist;
        dist = r;
    }
    g_touch_controls.joy_nx = (r > 0.0f) ? dx / r : 0.0f;
    g_touch_controls.joy_ny = (r > 0.0f) ? dy / r : 0.0f;
}

// ---- Finger event handlers ----

static void finger_down(SDL_FingerID id, float x, float y) {
    // Soft keyboard up (lobby code entry): taps must not synthesize game
    // keys — '\r', 'x', 'n' would leak into the code field (X and N are
    // code-alphabet letters). Same guard as Android; touch_tap on release
    // still fires, which is all the lobby needs (re-summon keyboard etc).
    if (SDL_IsTextInputActive()) return;

    float px = x * (float)s_w;
    float py = y * (float)s_h;

    // DEBUG: top-right corner → skip to next level
    if(x > 0.85f && y < 0.15f) {
        s_game->keyboard('n', 0, 0);
        return;
    }

    // Pause button hit zone: top-right, below score/multiplier (larger than visual circle)
    if(!g_touch_controls.pause_active &&
       tc_dist(px, py, g_touch_controls.pause_cx, g_touch_controls.pause_cy) <= g_touch_controls.pause_hit_radius) {
        g_touch_controls.pause_active = true;
        g_touch_controls.pause_finger = id;
        s_game->keyboard('\r', 0, 0);
        return;
    }

    // Centre-screen pause zone (invisible convenience area; the visible
    // top-right button is the primary control). Deliberately small — the
    // floating joystick claims the whole left half, and in narrow portrait
    // a left thumb strays past 0.30 of the width, so the old
    // 0.30..0.70 x 0.25..0.75 box paused mid-manoeuvre (matches
    // android_main.cpp).
    if(!g_touch_controls.pause_active &&
       x >= 0.38f && x <= 0.62f && y >= 0.30f && y <= 0.60f) {
        g_touch_controls.pause_active = true;
        g_touch_controls.pause_finger = id;
        s_game->keyboard('\r', 0, 0);
        return;
    }

    if(x < 0.5f) {
        // ---- Left half: virtual joystick (floating base) ----
        g_touch_controls.joy_cx     = px;
        g_touch_controls.joy_cy     = py;
        g_touch_controls.joy_nx     = 0.0f;
        g_touch_controls.joy_ny     = 0.0f;
        g_touch_controls.joy_active = true;
        g_touch_controls.joy_finger = id;
        // '\r' is ignored during gameplay but lets any tap start from the menu
        s_game->keyboard('\r', 0, 0);
    } else {
        // ---- Right half ----
        if(y < 0.4f) {
            // Top strip: menu / start key
            s_game->keyboard('\r', 0, 0);
        } else if(!g_touch_controls.shoot_pressed &&
                  tc_dist(px, py,
                          g_touch_controls.shoot_cx,
                          g_touch_controls.shoot_cy) <= g_touch_controls.btn_hit_radius) {
            g_touch_controls.shoot_pressed = true;
            g_touch_controls.shoot_finger  = id;
            s_game->keyboard(' ', 0, 0);
        } else if(g_touch_controls.mine_available &&
                  !g_touch_controls.mine_pressed &&
                  tc_dist(px, py,
                          g_touch_controls.mine_cx,
                          g_touch_controls.mine_cy) <= g_touch_controls.btn_hit_radius) {
            g_touch_controls.mine_pressed = true;
            g_touch_controls.mine_finger  = id;
            s_game->keyboard('x', 0, 0);
        }
        // Touches that don't hit a button are silently ignored.
    }
}

// The OS share sheet lives in ios_share.mm: UIKit's MacTypes.h defines a
// global `struct Point` that collides with the game's `class Point`, so
// this TU (which includes game headers) must not import UIKit.

static void finger_up(SDL_FingerID id, float x, float y) {
    // Forward tap position on finger-up so menu selections fire on release, not press
    s_game->touch_tap(x, y);

    if(g_touch_controls.pause_active && g_touch_controls.pause_finger == id) {
        g_touch_controls.pause_active = false;
        s_game->keyboard_up('p', 0, 0);
        return;
    }
    if(g_touch_controls.joy_active && g_touch_controls.joy_finger == id) {
        g_touch_controls.joy_active = false;
        g_touch_controls.joy_nx     = 0.0f;
        g_touch_controls.joy_ny     = 0.0f;
        // Immediately stop all movement
        s_game->touch_joystick(0.0f, 0.0f);
        // Pair the '\r' sent in finger_down for the left half
        s_game->keyboard_up('\r', 0, 0);
        return;
    }
    if(g_touch_controls.shoot_pressed && g_touch_controls.shoot_finger == id) {
        g_touch_controls.shoot_pressed = false;
        s_game->keyboard_up(' ', 0, 0);
        return;
    }
    if(g_touch_controls.mine_pressed && g_touch_controls.mine_finger == id) {
        g_touch_controls.mine_pressed = false;
        s_game->keyboard_up('x', 0, 0);
        return;
    }
    // Legacy: release '\r' (sent without finger tracking; just always release)
    s_game->keyboard_up('\r', 0, 0);
}

static void finger_motion(SDL_FingerID id, float x, float y) {
    if(!g_touch_controls.joy_active || g_touch_controls.joy_finger != id)
        return;

    float px = x * (float)s_w;
    float py = y * (float)s_h;
    update_joystick_nub(px, py);
}

// ============================================================
// SDL2 main
// ============================================================
// LAN beacon name (net_lan.cpp): defined in ios_device_name.mm, a
// Point-free TU (this file must not import UIKit — see the share-sheet
// note above finger_up).
extern "C" void ios_export_device_name(void);

extern "C" int SDL_main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    srand(time(NULL));

    // Export the device name for the LAN discovery beacon before any
    // state can open the lobby (same env bridge as Android).
    ios_export_device_name();

    // Orientation: follow the device, matching the Android build. Set
    // before any window is created.
    //
    // This hint is the ONE control that matters on iOS — SDL's
    // UIKit_GetSupportedOrientations gives a non-empty hint priority over
    // the window flags entirely (SDL_WINDOW_RESIZABLE, which is what
    // unlocks rotation on Android, is only consulted when the hint is
    // absent), and then intersects the result with the app's declared
    // orientations. So the hint and Info.plist's
    // UISupportedInterfaceOrientations have to be widened together: either
    // one left at landscape keeps the app pinned to landscape.
    //
    // PortraitUpsideDown is listed for iPad; SDL strips it on phones
    // itself, so a call is always answered the natural way up.
    SDL_SetHint(SDL_HINT_ORIENTATIONS,
                "LandscapeLeft LandscapeRight Portrait PortraitUpsideDown");

    // Initialise SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    // SDL2 starts with text input ACTIVE by default; the key-event guards
    // gated on SDL_IsTextInputActive() would otherwise swallow printable
    // keys forever. Only the lobby's code entry turns it back on.
    SDL_StopTextInput();

#ifdef NEWTONIA_NET_RTC
    // Headless netplay self-tests (mirrors xbox_main.cpp) — CI boots the
    // Simulator and launches with these env vars via SIMCTL_CHILD_*.
    {
        // exit(), not return: SDL_main runs inside UIApplicationMain on
        // iOS, and returning from it does not reliably terminate the
        // process — CI's `simctl launch --console-pty` then sat holding
        // the pty until its watchdog alarm fired (minutes of dead time
        // per PASSING test). A hard exit flushes stdio and ends the
        // process the moment the verdict is printed.
        const char *st = SDL_getenv("NEWTONIA_NET_SELFTEST");
        if (st && st[0] == '1' && st[1] == '\0') {
            SDL_Log("NEWTONIA_NET_SELFTEST: running loopback self-test...");
            bool ok = net_selftest();
            SDL_Log(ok ? "NET SELFTEST PASS" : "NET SELFTEST FAIL");
            exit(ok ? 0 : 1);
        }
        const char *ss = SDL_getenv("NEWTONIA_SIGNAL_SELFTEST");
        if (ss && ss[0] == '1' && ss[1] == '\0') {
            load_preferences();  // signal_url INI override applies here too
            SDL_Log("NEWTONIA_SIGNAL_SELFTEST: running relay self-test...");
            bool ok = net_signal_selftest();
            SDL_Log(ok ? "SIGNAL SELFTEST PASS" : "SIGNAL SELFTEST FAIL");
            exit(ok ? 0 : 1);
        }
    }
#endif

    // Request OpenGL ES 2.0 context
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,   16);

    SDL_DisplayMode dm;
    if (SDL_GetCurrentDisplayMode(0, &dm) == 0) {
        s_w = dm.w;
        s_h = dm.h;
    }

    s_window = SDL_CreateWindow("Newtonia",
                                SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                s_w, s_h,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN |
                                SDL_WINDOW_ALLOW_HIGHDPI);
    if (!s_window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    s_gl_ctx = SDL_GL_CreateContext(s_window);
    if (!s_gl_ctx) {
        SDL_Log("SDL_GL_CreateContext failed: %s", SDL_GetError());
        SDL_DestroyWindow(s_window);
        SDL_Quit();
        return 1;
    }

    // Retina: get actual drawable pixel dimensions (differ from logical points on HiDPI)
    SDL_GL_GetDrawableSize(s_window, &s_w, &s_h);

    SDL_GL_SetSwapInterval(1);   // vsync

    // Initialise GLES2 shim
    gles2_init();

    // Enable blending (same as the desktop init() function)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Audio
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) < 0)
        SDL_Log("Mix_OpenAudio failed: %s", Mix_GetError());
    // 64 channels; reserved: 2 for must-hear booms + WorldSound's
    // per-channel-volume pool — see glut.cpp / world_sound.h.
    Mix_AllocateChannels(64);
    Mix_ReserveChannels(WorldSound::FIRST_CHANNEL + WorldSound::POOL);

    // Game controller (physical gamepad via Bluetooth)
    SDL_JoystickEventState(SDL_ENABLE);
    SDL_GameController *controller = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            controller = SDL_GameControllerOpen(i);
            if (controller) break;
        }
    }

    // Game Center backend: sets the GameKit authenticateHandler, which kicks
    // off sign-in (the prompt is presented over this SDL window once it's up).
    Achievements::init();

    // Load user preferences before creating the state machine so that GLShip
    // constructors can read them (e.g. rotate_view).
    load_preferences();

    // Create the game state machine
    s_game = new StateManager();
    s_game->resize(s_w, s_h);
    Typer::resize(s_w, s_h);
    // Insets before touch layout — the pause button clears the inset-shifted
    // HUD row, so touch_controls_resize needs the notch inset already known.
    read_display_safe_insets();
    touch_controls_resize(s_w, s_h);

    Uint32 last_tick = SDL_GetTicks();

    while (s_running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT:
                s_running = false;
                break;

            // Keyboard: key events are the ONE source of typed characters —
            // the same scheme the Android device testing settled on. SDL's
            // iOS insertText synthesizes KEYDOWN/KEYUP per ASCII char
            // alongside SDL_TEXTINPUT, but on real hardware some presses
            // arrive with the key events only (Glenn: dropped letters made
            // code entry nearly impossible feeding from TEXTINPUT), so
            // TEXTINPUT is not forwarded and KEYDOWN passes unconditionally
            // (which also covers Bluetooth keyboards).
            case SDL_TEXTINPUT:
                break;
            case SDL_KEYDOWN: {
                if (e.key.repeat) break; // game tracks held state itself; ignore SDL repeats
                SDL_Keycode k = e.key.keysym.sym;
                // Escape (the simulator's Mac keyboard, Bluetooth keyboards)
                // = Android Back: back out one level via back_pressed().
                // Never stops the loop — an iOS app that tears down its
                // window doesn't exit, it just sits on a black screen.
                if (k == SDLK_ESCAPE) { s_game->back_pressed(); break; }
                unsigned char key = (k < 128) ? (unsigned char)k : 0;
                if (key) s_game->keyboard(key, 0, 0);
                break;
            }
            case SDL_KEYUP: {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE) break; // handled via back_pressed() on keydown
                unsigned char key = (k < 128) ? (unsigned char)k : 0;
                if (key) s_game->keyboard_up(key, 0, 0);
                break;
            }

            // Touch input
            case SDL_FINGERDOWN:
                finger_down(e.tfinger.fingerId,
                            e.tfinger.x, e.tfinger.y);
                break;
            case SDL_FINGERUP:
                finger_up(e.tfinger.fingerId, e.tfinger.x, e.tfinger.y);
                break;
            case SDL_FINGERMOTION:
                finger_motion(e.tfinger.fingerId,
                              e.tfinger.x, e.tfinger.y);
                break;

            // App lifecycle: auto-pause when backgrounded, auto-resume when foregrounded.
            // SDL2 fires SDL_APP_* events on some versions/configurations and
            // SDL_WINDOWEVENT focus events on others; handle both so we catch it.
            case SDL_APP_WILLENTERBACKGROUND:
                touch_controls_reset(s_game);
                s_game->focus_lost();
                break;
            case SDL_APP_DIDENTERFOREGROUND:
                s_game->focus_gained();
                s_reset_tick = true;
                break;
            case SDL_WINDOWEVENT:
                if(e.window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
                   e.window.event == SDL_WINDOWEVENT_MINIMIZED) {
                    touch_controls_reset(s_game);
                    s_game->focus_lost();
                } else if(e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED ||
                          e.window.event == SDL_WINDOWEVENT_RESTORED) {
                    s_game->focus_gained();
                    s_reset_tick = true;
                } else if(e.window.event == SDL_WINDOWEVENT_RESIZED ||
                          e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    // Device rotation. The UIKit backend posts RESIZED,
                    // which SDL turns into a SIZE_CHANGED as well; both are
                    // handled because apply_window_size() no-ops on an
                    // unchanged drawable, so the pair costs nothing.
                    apply_window_size();
                }
                break;

            // Game controller
            default:
                s_game->controller(e);
                break;
            }
        }

        // Apply continuous joystick state every tick
        if(g_touch_controls.joy_active) {
            s_game->touch_joystick(g_touch_controls.joy_nx, g_touch_controls.joy_ny);
        }

        Uint32 now   = SDL_GetTicks();
        if(s_reset_tick) {
            // App just returned from background; discard the elapsed time so the
            // simulation doesn't try to catch up on the entire suspended period.
            last_tick    = now;
            s_reset_tick = false;
        }
        int    delta = (int)(now - last_tick);
        last_tick    = now;

        s_game->tick(delta);

        // Draw game
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        s_game->draw();

        SDL_GL_SwapWindow(s_window);
    }

    // Cleanup
    delete s_game;
    Asteroid::free_sounds();
    Typer::cleanup();
    gles2_shutdown();
    Mix_CloseAudio();
    if (controller) SDL_GameControllerClose(controller);
    SDL_GL_DeleteContext(s_gl_ctx);
    SDL_DestroyWindow(s_window);
    SDL_Quit();
    return 0;
}

#endif // __IOS__
