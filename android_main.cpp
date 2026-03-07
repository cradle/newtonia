// Android entry point using SDL2.
// Replaces glut.cpp on Android: handles window creation, the game loop,
// touch → keyboard/controller event mapping, and audio initialisation.

#ifdef __ANDROID__

#include <SDL.h>
#include <SDL_mixer.h>
#include <GLES2/gl2.h>

#include "gles2_compat.h"
#include "state_manager.h"
#include "typer.h"

#include <iostream>
#include <cmath>

// ============================================================
// Touch → game-input mapping
// ============================================================
// We split the screen into a left half (movement) and right half (actions).
//
// Left half regions (normalised 0-1 within the left half):
//   top-left    quarter → rotate left  (key 'a')
//   top-right   quarter → rotate right (key 'd')
//   center-top          → thrust        (key 'w')
//   center-bottom       → reverse       (key 's')
//
// Right half regions:
//   bottom-left  → shoot  (key ' ')
//   bottom-right → mine   (key 'x')
//   top          → start/enter ('\r')
//
// Each active touch finger is mapped to a virtual key.  We synthesise
// keyboard_down / keyboard_up events as fingers appear / disappear.

struct FingerKey {
    SDL_FingerID finger_id;
    unsigned char key;
};

static const int MAX_FINGERS = 10;
static FingerKey s_finger_keys[MAX_FINGERS];
static int       s_finger_count = 0;

static StateManager *s_game     = nullptr;
static SDL_Window   *s_window   = nullptr;
static SDL_GLContext s_gl_ctx   = nullptr;
static int           s_w = 800, s_h = 600;
static bool          s_running  = true;

static unsigned char touch_to_key(float norm_x, float norm_y) {
    // norm_x, norm_y are 0-1 relative to whole screen
    bool left_half = (norm_x < 0.5f);
    if (left_half) {
        float lx = norm_x * 2.0f;        // 0-1 within left half
        float ly = norm_y;
        if (ly < 0.4f) {
            return 'w';                   // thrust
        } else if (ly > 0.6f) {
            return 's';                   // reverse
        } else if (lx < 0.5f) {
            return 'a';                   // rotate left
        } else {
            return 'd';                   // rotate right
        }
    } else {
        float rx = (norm_x - 0.5f) * 2.0f; // 0-1 within right half
        float ry = norm_y;
        if (ry < 0.35f) {
            return '\r';                  // enter / start
        } else if (rx < 0.5f) {
            return ' ';                   // shoot
        } else {
            return 'x';                   // mine
        }
    }
}

static void finger_down(SDL_FingerID id, float x, float y) {
    if (s_finger_count >= MAX_FINGERS) return;
    unsigned char key = touch_to_key(x, y);
    s_finger_keys[s_finger_count++] = {id, key};
    s_game->keyboard(key, 0, 0);
}

static void finger_up(SDL_FingerID id) {
    for (int i = 0; i < s_finger_count; i++) {
        if (s_finger_keys[i].finger_id == id) {
            s_game->keyboard_up(s_finger_keys[i].key, 0, 0);
            s_finger_keys[i] = s_finger_keys[--s_finger_count];
            return;
        }
    }
}

// ============================================================
// Touch control HUD (simple coloured rectangles)
// ============================================================
static void draw_hud() {
    // Draw semi-transparent touch zone overlays so the player can see the
    // control areas.  We keep it minimal: just faint outlines.

    // Left half zones ── four quadrants
    struct Zone { float x, y, w, h; float r, g, b; const char *label; };
    float sw = (float)s_w, sh = (float)s_h;
    Zone zones[] = {
        // left half: thrust (top), reverse (bottom), left/right (middle)
        {0,    0,    sw*0.5f, sh*0.4f,   0.2f,0.8f,0.2f, "THRUST"},
        {0,    sh*0.6f, sw*0.25f, sh*0.4f, 0.8f,0.2f,0.2f, "LEFT"},
        {sw*0.25f, sh*0.6f, sw*0.25f, sh*0.4f, 0.2f,0.2f,0.8f, "RIGHT"},
        {0,    sh*0.4f, sw*0.25f, sh*0.2f, 0.8f,0.8f,0.2f, "L-TURN"},
        {sw*0.25f,sh*0.4f,sw*0.25f,sh*0.2f,0.8f,0.8f,0.2f, "R-TURN"},
        // right half
        {sw*0.5f, 0,    sw*0.5f, sh*0.35f, 0.6f,0.6f,0.6f, "START"},
        {sw*0.5f, sh*0.35f, sw*0.25f, sh*0.65f, 0.9f,0.5f,0.1f, "SHOOT"},
        {sw*0.75f,sh*0.35f, sw*0.25f, sh*0.65f, 0.5f,0.1f,0.9f, "MINE"},
    };

    // Save matrices; set up pixel-space ortho for the HUD
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, sw, sh, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    for (auto &z : zones) {
        glColor4f(z.r, z.g, z.b, 0.08f);
        glBegin(GL_POLYGON);
        glVertex2f(z.x,        z.y);
        glVertex2f(z.x + z.w,  z.y);
        glVertex2f(z.x + z.w,  z.y + z.h);
        glVertex2f(z.x,        z.y + z.h);
        glEnd();
        glColor4f(z.r, z.g, z.b, 0.3f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(z.x + 2,        z.y + 2);
        glVertex2f(z.x + z.w - 2,  z.y + 2);
        glVertex2f(z.x + z.w - 2,  z.y + z.h - 2);
        glVertex2f(z.x + 2,        z.y + z.h - 2);
        glEnd();
    }

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
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
                // Re-evaluate which zone the finger is in
                finger_up(e.tfinger.fingerId);
                finger_down(e.tfinger.fingerId,
                            e.tfinger.x, e.tfinger.y);
                break;

            // Game controller
            default:
                s_game->controller(e);
                break;
            }
        }

        Uint32 now   = SDL_GetTicks();
        int    delta = (int)(now - last_tick);
        last_tick    = now;

        s_game->tick(delta);

        // Draw game
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        s_game->draw();

        // Draw touch control HUD on top
        draw_hud();

        SDL_GL_SwapWindow(s_window);
    }

    // Cleanup
    delete s_game;
    gles2_shutdown();
    Mix_CloseAudio();
    if (controller) SDL_GameControllerClose(controller);
    SDL_GL_DeleteContext(s_gl_ctx);
    SDL_DestroyWindow(s_window);
    SDL_Quit();
    return 0;
}

#endif // __ANDROID__
