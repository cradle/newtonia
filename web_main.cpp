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
#include <emscripten/html5.h>
#include <SDL.h>
#include <SDL_mixer.h>

#include "gles2_compat.h"
#include "state_manager.h"
#include "typer.h"
#include "asteroid.h"
#include "preferences.h"
#include "invites.h"

#include <cmath>
#include <cstdlib>
#include <ctime>
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

// Pause zone finger tracking (top-centre, over the LEVEL text)
static bool          s_pause_active = false;
static SDL_FingerID  s_pause_finger = 0;

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
        if (norm_y < 0.65f) return 0;  // dead zone above buttons
        float rx = (norm_x - 0.5f) * 2.0f;
        if (rx < 0.5f)  return ' ';  // shoot
        else            return 'x';  // mine
    }
}

static void finger_down(SDL_FingerID id, float x, float y) {
    // Pause button: top-right, below score/multiplier
    if(!s_pause_active && x >= 0.75f && y < 0.25f) {
        s_pause_active = true;
        s_pause_finger = id;
        s_game->keyboard('\r', 0, 0);
        return;
    }

    // Centre-screen pause zone (large invisible area, avoids edges used by controls)
    if(!s_pause_active && x >= 0.30f && x <= 0.70f && y >= 0.25f && y <= 0.75f) {
        s_pause_active = true;
        s_pause_finger = id;
        s_game->keyboard('\r', 0, 0);
        return;
    }
    if (s_finger_count >= MAX_FINGERS) return;
    unsigned char key = touch_to_key(x, y);
    if (!key) return;
    s_finger_keys[s_finger_count++] = {id, key};
    s_game->keyboard(key, 0, 0);
}

static void finger_up(SDL_FingerID id, float x, float y) {
    // Forward tap position on finger-up so menu selections fire on release, not press
    s_game->touch_tap(x, y);

    if(s_pause_active && s_pause_finger == id) {
        s_pause_active = false;
        s_game->keyboard_up('p', 0, 0);
        return;
    }
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
            if (e.key.repeat) break; // game tracks held state itself; ignore SDL repeats
            SDL_Keycode k = e.key.keysym.sym;
            unsigned char key = (k < 128) ? (unsigned char)k : 0;
            if (!key) switch (k) {
                case SDLK_F1:    key = 128 + GLUT_KEY_F1;    break;
                case SDLK_F4:    key = 128 + GLUT_KEY_F4;    break;
                case SDLK_F8:    key = 128 + GLUT_KEY_F8;    break;
                case SDLK_F11:   key = 128 + GLUT_KEY_F11;   break;
                // Arrows use desktop GLUT's special-key codes; menus alias
                // them to WASD (State::nav_key).
                case SDLK_UP:    key = 128 + GLUT_KEY_UP;    break;
                case SDLK_DOWN:  key = 128 + GLUT_KEY_DOWN;  break;
                case SDLK_LEFT:  key = 128 + GLUT_KEY_LEFT;  break;
                case SDLK_RIGHT: key = 128 + GLUT_KEY_RIGHT; break;
                default: break;
            }
            if (key) s_game->keyboard(key, 0, 0);
            break;
        }
        case SDL_KEYUP: {
            SDL_Keycode k = e.key.keysym.sym;
            unsigned char key = (k < 128) ? (unsigned char)k : 0;
            if (!key) switch (k) {
                case SDLK_F1:    key = 128 + GLUT_KEY_F1;    break;
                case SDLK_F4:    key = 128 + GLUT_KEY_F4;    break;
                case SDLK_F8:    key = 128 + GLUT_KEY_F8;    break;
                case SDLK_F11:   key = 128 + GLUT_KEY_F11;   break;
                case SDLK_UP:    key = 128 + GLUT_KEY_UP;    break;
                case SDLK_DOWN:  key = 128 + GLUT_KEY_DOWN;  break;
                case SDLK_LEFT:  key = 128 + GLUT_KEY_LEFT;  break;
                case SDLK_RIGHT: key = 128 + GLUT_KEY_RIGHT; break;
                default: break;
            }
            if (key) s_game->keyboard_up(key, 0, 0);
            break;
        }

        case SDL_FINGERDOWN:
            finger_down(e.tfinger.fingerId, e.tfinger.x, e.tfinger.y);
            break;
        case SDL_FINGERUP:
            finger_up(e.tfinger.fingerId, e.tfinger.x, e.tfinger.y);
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
                // Don't trust data1/data2: depending on the SDL emscripten
                // backend's external-size detection they are either CSS pixels
                // or an echo of the last SDL_SetWindowSize — which we already
                // DPR-scaled. Multiplying the echo by DPR again inflates the
                // drawing buffer DPR× on EVERY browser resize (phones fire one
                // per URL-bar show/hide and app switch), compounding until the
                // canvas allocation fails. The CSS downscale of an oversized
                // buffer then drops the antialiased thick-line cores — long
                // axis-aligned strokes (the menu title) go uniformly faint
                // while diagonals stay bright. Re-reading the canvas's CSS
                // size makes this idempotent: repeated events converge on the
                // same buffer size, so no feedback is possible.
                double css_w = 0, css_h = 0;
                double dpr = emscripten_get_device_pixel_ratio();
                int w, h;
                if (emscripten_get_element_css_size("#canvas", &css_w, &css_h) == EMSCRIPTEN_RESULT_SUCCESS
                        && css_w >= 1.0 && css_h >= 1.0) {
                    w = (int)(css_w * dpr);
                    h = (int)(css_h * dpr);
                } else {
                    w = (int)(e.window.data1 * dpr);
                    h = (int)(e.window.data2 * dpr);
                }
                if (w != s_w || h != s_h) {
                    s_w = w;
                    s_h = h;
                    SDL_SetWindowSize(s_window, s_w, s_h);
                    s_game->resize(s_w, s_h);
                    Typer::resize(s_w, s_h);
                    SDL_Log("web: resize css %.0fx%.0f dpr %.2f buffer %dx%d",
                            css_w, css_h, dpr, s_w, s_h);
                }
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

// A universal join link (https://newtonia.metonymous.com/join?code=XXXX)
// opened the web game with ?code=; main.ts pulls the code out and calls this
// so Menu::tick's Invites::poll_accepted_invite jumps straight into the lobby
// as a joiner — the same handoff Steam/iOS/Android deep links use.
extern "C" EMSCRIPTEN_KEEPALIVE void web_accept_invite(const char *code) {
    if (code && code[0]) Invites::note_accepted(code);
}

// Called from the JS menu overlay on touchend with normalised [0,1] tap position.
// touch_tap() handles the Continue/New Game split (when a save exists).
// keyboard_up('\r') handles the no-save "tap to start" case (touch_tap is a no-op then).
extern "C" EMSCRIPTEN_KEEPALIVE void web_menu_tap(float nx, float ny) {
    if (!s_game) return;
    s_game->touch_tap(nx, ny);
    s_game->keyboard_up('\r', 0, 0);
}

// Background heartbeat for online sessions: hidden/occluded tabs stop
// requestAnimationFrame entirely, so the main loop starves and snapshots
// apply in rare bursts (host stops simulating; the peer's game stalls).
// A plain interval keeps ticking at the browser's clamped background rate
// (~1 Hz — pages with a live WebRTC connection are exempt from Chrome's
// harsher 1-per-minute throttling). Solo play intentionally stays frozen
// while hidden, like a pause.
extern "C" EMSCRIPTEN_KEEPALIVE void web_background_tick() {
    if (!s_idb_ready || !s_game) return;
    if (!s_game->wants_background_ticks()) return;
    // Cap the catch-up burst: if the browser throttled us harder than
    // expected, don't try to simulate a minute of world time in one call.
    Uint32 now = SDL_GetTicks();
    if (now - s_last_tick > 2000) s_last_tick = now - 2000;
    main_loop();
}

// Called from JS after FS.syncfs(true) completes (IDBFS → memory).
// Initialises the StateManager then releases the main loop gate.
// EMSCRIPTEN_KEEPALIVE exports this so JS can call Module._web_on_idb_ready().
extern "C" EMSCRIPTEN_KEEPALIVE void web_on_idb_ready() {
    // By the time IDBFS sync completes the browser has finished its initial
    // layout, so we can query the canvas's actual CSS display size.  This is
    // more accurate than SDL_GetWindowSize() × DPR (which reflects the logical
    // size passed to SDL_CreateWindow, not the CSS-constrained canvas area).
    // Getting it right here prevents the 1–2× upscale blur that would otherwise
    // persist until the first SDL_WINDOWEVENT_RESIZED (e.g. going fullscreen).
    {
        double css_w = 0, css_h = 0;
        if (emscripten_get_element_css_size("#canvas", &css_w, &css_h) == EMSCRIPTEN_RESULT_SUCCESS
                && css_w >= 1.0 && css_h >= 1.0) {
            double dpr = emscripten_get_device_pixel_ratio();
            s_w = (int)(css_w * dpr);
            s_h = (int)(css_h * dpr);
            SDL_SetWindowSize(s_window, s_w, s_h);
        }
    }
    // IDBFS is now populated — load preferences before constructing the
    // StateManager so GLShip constructors can read them (e.g. rotate_view).
    load_preferences();

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
    srand(time(NULL));

    // Debug bridge (the web twin of NewtoniaActivity's intent-extra bridge):
    // NEWTONIA_* URL query params — or localStorage entries — become process
    // env vars. Directly-served builds take
    //   ?NEWTONIA_BETA=1&NEWTONIA_START_GENERATION=9&NEWTONIA_ALL_WEAPONS=1;
    // on itch (the only web deploy of the netplay branch) the game sits in an
    // iframe whose URL isn't editable, so set the knobs from the remote-
    // inspection console instead:
    //   localStorage.NEWTONIA_START_GENERATION = 9; location.reload()
    // (localStorage.clear() to reset). URL params win over localStorage. All
    // current debug knobs take numeric values, so only integers pass — the
    // bridge stays trivial and nothing attacker-shaped rides a shared link;
    // worst case is a cheat-flagged test game.
    {
        static const char *kDebugVars[] = {
            "NEWTONIA_BETA", "NEWTONIA_START_GENERATION",
            "NEWTONIA_ALL_WEAPONS", "NEWTONIA_FRAME_LOG",
            "NEWTONIA_LINE_EMULATION", "NEWTONIA_TEST_SPAWN_PICKUPS", NULL };
        for (int i = 0; kDebugVars[i]; i++) {
            int v = EM_ASM_INT({
                try {
                    var k = UTF8ToString($0);
                    var val = new URLSearchParams(location.search).get(k);
                    if (val === null || val === '') {
                        try { val = localStorage.getItem(k); } catch (e) {}
                    }
                    if (val === null || val === '') return -1;
                    var n = parseInt(val, 10);
                    return isNaN(n) ? -1 : n;
                } catch (e) { return -1; }
            }, kDebugVars[i]);
            if (v >= 0) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", v);
                setenv(kDebugVars[i], buf, 1);
                printf("web: env %s=%s (from URL/localStorage)\n",
                       kDebugVars[i], buf);
            }
        }
    }

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

    // Scale the canvas to physical pixels based on its actual CSS layout size.
    // SDL_GetWindowSize returns the SDL_CreateWindow size (800×600), not the CSS
    // layout size determined by the browser — so we use emscripten_get_element_css_size
    // to read the real canvas dimensions before the game initialises.
    {
        double dpr = emscripten_get_device_pixel_ratio();
        double cssW = 0, cssH = 0;
        emscripten_get_element_css_size("#canvas", &cssW, &cssH);
        if (cssW > 0 && cssH > 0) {
            s_w = (int)(cssW * dpr);
            s_h = (int)(cssH * dpr);
        } else {
            // CSS layout not ready yet — fall back to SDL size scaled by DPR.
            SDL_GetWindowSize(s_window, &s_w, &s_h);
            s_w = (int)(s_w * dpr);
            s_h = (int)(s_h * dpr);
        }
        emscripten_set_canvas_element_size("#canvas", s_w, s_h);
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
    // 64 channels + 2 reserved for must-hear booms — see glut.cpp.
    Mix_AllocateChannels(64);
    Mix_ReserveChannels(2);

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
    char *raw_pref = SDL_GetPrefPath("cc.gfm", "newtonia");
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

    // Background heartbeat: rAF stops in hidden tabs, so an interval pumps
    // web_background_tick() instead (it no-ops unless an online session or
    // lobby is active — see wants_background_ticks). 500 ms nominal; the
    // browser clamps it to ~1 s while hidden.
    EM_ASM({
        setInterval(function() {
            if (document.hidden) Module._web_background_tick();
        }, 500);
    });

    // emscripten_set_main_loop stays in main() so the WebGL context is never
    // torn down.  The loop returns early until s_idb_ready is set.
    emscripten_set_main_loop(main_loop, 0, 1);

    // Unreachable, but kept for clarity:
    delete s_game;
    Asteroid::free_sounds();
    Typer::cleanup();
    gles2_shutdown();
    Mix_CloseAudio();
    if (s_controller) SDL_GameControllerClose(s_controller);
    SDL_GL_DeleteContext(s_gl_ctx);
    SDL_DestroyWindow(s_window);
    SDL_Quit();
    return 0;
}

#endif // __EMSCRIPTEN__
