// Web (Emscripten/WebAssembly) entry point.
// Compiles the game to WebAssembly + WebGL via Emscripten.
// Mirrors android_main.cpp but uses emscripten_set_main_loop() instead of a
// blocking while-loop, because the browser controls the event loop.
//
// High-score persistence
// ----------------------
// SDL_GetPrefPath() returns an in-memory MEMFS path.  We mount an IDBFS
// (IndexedDB-backed) filesystem over that path so the highscore.dat file
// survives page refreshes.  The mount + initial sync happen asynchronously;
// web_on_idb_ready() is called from JS when the sync completes, and only
// then do we create the StateManager and start the game loop.

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <SDL.h>
#include <SDL_mixer.h>

#include "gles2_compat.h"
#include "state_manager.h"
#include "typer.h"

#include <cmath>
#include <string>

// ============================================================
// Touch → game-input mapping (same regions as android_main.cpp)
// ============================================================
struct FingerKey {
    SDL_FingerID finger_id;
    unsigned char key;
};

static const int MAX_FINGERS = 10;
static FingerKey s_finger_keys[MAX_FINGERS];
static int       s_finger_count = 0;

static StateManager    *s_game    = nullptr;
static SDL_Window      *s_window  = nullptr;
static SDL_GLContext    s_gl_ctx  = nullptr;
static int              s_w = 800, s_h = 600;
static Uint32           s_last_tick = 0;
static SDL_GameController *s_controller = nullptr;
// Set to true by web_on_idb_ready() once IDBFS has been populated.
// The main loop skips tick/draw until this flag is set.
static bool             s_idb_ready = false;

static unsigned char touch_to_key(float norm_x, float norm_y) {
    if (norm_x > 0.85f && norm_y < 0.15f)
        return 'n'; // debug: skip level

    bool left_half = (norm_x < 0.5f);
    if (left_half) {
        float lx = norm_x * 2.0f;
        float ly = norm_y;
        if (ly < 0.4f)       return 'w'; // thrust
        else if (ly > 0.6f)  return 's'; // reverse
        else if (lx < 0.5f)  return 'a'; // rotate left
        else                 return 'd'; // rotate right
    } else {
        float rx = (norm_x - 0.5f) * 2.0f;
        if (rx < 0.5f)  return ' ';  // shoot
        else            return 'x';  // mine
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
// Main loop — called by Emscripten once per animation frame
// ============================================================
static void main_loop() {
    // Hold until IDBFS sync completes so StateManager is initialised.
    if (!s_idb_ready) return;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            emscripten_cancel_main_loop();
            return;

        case SDL_KEYDOWN: {
            SDL_Keycode k = e.key.keysym.sym;
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

        case SDL_FINGERDOWN:
            finger_down(e.tfinger.fingerId, e.tfinger.x, e.tfinger.y);
            break;
        case SDL_FINGERUP:
            finger_up(e.tfinger.fingerId);
            break;
        case SDL_FINGERMOTION: {
            unsigned char new_key = touch_to_key(e.tfinger.x, e.tfinger.y);
            for (int i = 0; i < s_finger_count; i++) {
                if (s_finger_keys[i].finger_id == e.tfinger.fingerId) {
                    if (s_finger_keys[i].key != new_key) {
                        s_game->keyboard_up(s_finger_keys[i].key, 0, 0);
                        s_finger_keys[i].key = new_key;
                        s_game->keyboard(new_key, 0, 0);
                    }
                    break;
                }
            }
            break;
        }

        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_RESIZED) {
                // e.window.data1/data2 are CSS pixels; scale to physical pixels
                // so the canvas renders at full device resolution.
                double dpr = emscripten_get_device_pixel_ratio();
                s_w = (int)(e.window.data1 * dpr);
                s_h = (int)(e.window.data2 * dpr);
                SDL_SetWindowSize(s_window, s_w, s_h);
                s_game->resize(s_w, s_h);
                Typer::resize(s_w, s_h);
            }
            break;

        default:
            s_game->controller(e);
            break;
        }
    }

    Uint32 now   = SDL_GetTicks();
    int    delta = (int)(now - s_last_tick);
    s_last_tick  = now;

    s_game->tick(delta);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    s_game->draw();

    SDL_GL_SwapWindow(s_window);
}

// Called from JS touch controls to apply analog joystick input directly.
// nx/ny are normalised [-1, 1]; ny positive = down on screen = reverse thrust.
extern "C" EMSCRIPTEN_KEEPALIVE void web_touch_joystick(float nx, float ny) {
    if (s_game) s_game->touch_joystick(nx, ny);
}

// Called from JS after FS.syncfs(true) completes (IDBFS → memory).
// Initialises the StateManager then releases the main loop gate.
// EMSCRIPTEN_KEEPALIVE exports this so JS can call Module._web_on_idb_ready().
extern "C" EMSCRIPTEN_KEEPALIVE void web_on_idb_ready() {
    s_game = new StateManager();
    s_game->resize(s_w, s_h);
    Typer::resize(s_w, s_h);
    s_last_tick = SDL_GetTicks();
    s_idb_ready = true; // open the main_loop gate
}

// ============================================================
// main
// ============================================================
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    // Request WebGL 1.0 (OpenGL ES 2.0 profile)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);

    s_window = SDL_CreateWindow("Newtonia",
                                SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                s_w, s_h,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
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

    // Scale the initial canvas to physical pixels using devicePixelRatio so
    // the game renders at full device resolution instead of CSS pixel resolution.
    {
        double dpr = emscripten_get_device_pixel_ratio();
        SDL_GetWindowSize(s_window, &s_w, &s_h);
        s_w = (int)(s_w * dpr);
        s_h = (int)(s_h * dpr);
        SDL_SetWindowSize(s_window, s_w, s_h);
    }

    SDL_GL_SetSwapInterval(1); // vsync

    gles2_init();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Audio — may silently fail before first user gesture (browser policy).
    // SDL2_mixer on Emscripten defers actual playback until unlocked.
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) < 0)
        SDL_Log("Mix_OpenAudio failed: %s", Mix_GetError());

    SDL_JoystickEventState(SDL_ENABLE);
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            s_controller = SDL_GameControllerOpen(i);
            if (s_controller) break;
        }
    }

    // ---- IDBFS: mount persistent storage over the SDL pref path ----
    // SDL_GetPrefPath creates an empty MEMFS directory; we overlay IDBFS on
    // top so reads/writes automatically go through IndexedDB.
    // The async sync (IDB → memory) completes before start_game() is called.
    char *raw_pref = SDL_GetPrefPath("org.newtonia", "newtonia");
    if (raw_pref) {
        // Strip trailing slash — FS.mount needs the dir itself, not a child path.
        std::string pref(raw_pref);
        SDL_free(raw_pref);
        if (!pref.empty() && pref.back() == '/') pref.pop_back();

        EM_ASM({
            var path = UTF8ToString($0);
            try {
                FS.mount(IDBFS, {}, path);
            } catch (e) {
                console.warn('[newtonia] IDBFS mount failed:', e);
            }
            // Populate memory from IndexedDB, then open the main loop gate.
            FS.syncfs(true, function(err) {
                if (err) console.error('[newtonia] IDBFS initial sync failed:', err);
                Module._web_on_idb_ready();
            });
        }, pref.c_str());
    } else {
        // No pref path — initialise without persistence.
        web_on_idb_ready();
    }

    // emscripten_set_main_loop stays in main() so the WebGL context is never
    // torn down.  The loop returns early until s_idb_ready is set.
    emscripten_set_main_loop(main_loop, 0, 1);

    // Unreachable, but kept for clarity:
    delete s_game;
    gles2_shutdown();
    Mix_CloseAudio();
    if (s_controller) SDL_GameControllerClose(s_controller);
    SDL_GL_DeleteContext(s_gl_ctx);
    SDL_DestroyWindow(s_window);
    SDL_Quit();
    return 0;
}

#endif // __EMSCRIPTEN__
