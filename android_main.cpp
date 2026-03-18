// Android entry point using SDL2.
// Replaces glut.cpp on Android: handles window creation, the game loop,
// touch → keyboard/controller event mapping, and audio initialisation.

#ifdef __ANDROID__

#include <SDL.h>
#include <SDL_mixer.h>
#include <GLES2/gl2.h>

#include "gles2_compat.h"
#include "state_manager.h"
#include "touch_controls.h"
#include "typer.h"
#include "asteroid.h"

#include <iostream>
#include <cmath>

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
    float px = x * (float)s_w;
    float py = y * (float)s_h;

    // DEBUG: top-right corner → skip to next level
    if(x > 0.85f && y < 0.15f) {
        s_game->keyboard('n', 0, 0);
        return;
    }

    // Pause zone: top-centre over the LEVEL text
    if(!g_touch_controls.pause_active &&
       tc_dist(px, py, g_touch_controls.pause_cx, g_touch_controls.pause_cy) <= g_touch_controls.pause_radius) {
        g_touch_controls.pause_active = true;
        g_touch_controls.pause_finger = id;
        s_game->keyboard('\r', 0, 0);  // allow menu start on same tap
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
        } else if(!g_touch_controls.mine_pressed &&
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

static void finger_up(SDL_FingerID id) {
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
extern "C" int SDL_main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    // Initialise SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

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
                                SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
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

    SDL_GL_SetSwapInterval(1);   // vsync

    // Initialise GLES2 shim
    gles2_init();

    // Enable blending (same as the desktop init() function)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Audio
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) < 0)
        SDL_Log("Mix_OpenAudio failed: %s", Mix_GetError());
    Mix_AllocateChannels(32);

    // Game controller (Android may have a physical gamepad via USB/BT)
    SDL_JoystickEventState(SDL_ENABLE);
    SDL_GameController *controller = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            controller = SDL_GameControllerOpen(i);
            if (controller) break;
        }
    }

    // Create the game state machine
    s_game = new StateManager();
    s_game->resize(s_w, s_h);
    Typer::resize(s_w, s_h);
    touch_controls_resize(s_w, s_h);

    Uint32 last_tick = SDL_GetTicks();

    while (s_running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT:
                s_running = false;
                break;

            // Physical keyboard (Bluetooth keyboard, emulator, etc.)
            case SDL_KEYDOWN: {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE) { s_running = false; break; }
                unsigned char key = (k < 128) ? (unsigned char)k : 0;
                if (key) s_game->keyboard(key, 0, 0);
                break;
            }
            case SDL_KEYUP: {
                SDL_Keycode k = e.key.keysym.sym;
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
                finger_up(e.tfinger.fingerId);
                break;
            case SDL_FINGERMOTION:
                finger_motion(e.tfinger.fingerId,
                              e.tfinger.x, e.tfinger.y);
                break;

            // App lifecycle: auto-pause when backgrounded, auto-resume when foregrounded.
            // SDL2 on Android fires SDL_APP_* events on some versions/configurations
            // and SDL_WINDOWEVENT focus events on others; handle both so we catch it.
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
    gles2_shutdown();
    Mix_CloseAudio();
    if (controller) SDL_GameControllerClose(controller);
    SDL_GL_DeleteContext(s_gl_ctx);
    SDL_DestroyWindow(s_window);
    SDL_Quit();
    return 0;
}

#endif // __ANDROID__
