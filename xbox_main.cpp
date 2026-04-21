// GDK entry point — Xbox console (_GAMING_XBOX) and GDK Desktop (_GAMING_DESKTOP).
//
// Uses SDL2 with OpenGL ES 2.0 via ANGLE, which translates GLES calls to
// Direct3D 11 — the same path used on Android / Web, avoiding a full graphics
// rewrite.  The GDK CMake toolchain (see xbox/CMakeLists.txt) links the ANGLE
// libraries (libEGL / libGLESv2) that ship with the GDK.
//
// Mirrors android_main.cpp: SDL2 event loop, SDL_GameController input,
// focus-lost / focus-gained lifecycle events required by GDK certification.
//
// Platform differences
// --------------------
// _GAMING_XBOX    : fullscreen at native display resolution; no resize events.
// _GAMING_DESKTOP : 1280×720 resizable window; handles SDL_WINDOWEVENT_RESIZED.
//
// Controller mapping
// ------------------
// The Xbox controller is fully handled by GLShip via StateManager::controller().
// Keyboard fallbacks (WASD / Space / X) are also accepted (primary on Desktop).
//
// Save data
// ---------
// SDL_GetPrefPath() under the GDK returns a path inside the title's
// LocalState storage, which is automatically persisted by the OS.

#if defined(_GAMING_XBOX) || defined(_GAMING_DESKTOP)

// Prevent SDL_main.h from renaming main() to SDL_main().  We use the CRT
// console entry point directly and call SDL_SetMainReady() ourselves.
// Without this, the CRT startup looks for main() (which SDL renamed) and
// the linker fails with "unresolved external symbol main".
#define SDL_MAIN_HANDLED

#include <SDL.h>
#include <SDL_mixer.h>

// Direct GDK PLM headers — Xbox console only.
// SDL2's Win32 backend does not hook GDK PLM, so we register suspend
// acknowledgment callbacks directly to satisfy Xbox certification.
#ifdef _GAMING_XBOX
#  include <XSuspendResume.h>
#  include <XTaskQueue.h>
#endif

// SDL2's WIN_PumpEvents calls GDK_DispatchTaskQueue() when __GDK__ is defined,
// but the implementation lives in SDL2's GDK backend (SDL_gdk.cpp) which
// CMake doesn't add when using the VS2022-platform approach.
// A no-op is safe: our event loop handles GDK PLM lifecycle events directly
// via SDL_APP_WILLENTERBACKGROUND / SDL_APP_DIDENTERFOREGROUND.
extern "C" void GDK_DispatchTaskQueue(void) {}

#include "gles2_compat.h"
#include "state_manager.h"
#include "typer.h"
#include "asteroid.h"
#include "preferences.h"

#include <cstdlib>
#include <ctime>
#ifdef _GAMING_DESKTOP
#include <windows.h>
#endif

static StateManager    *s_game       = nullptr;
static SDL_Window      *s_window     = nullptr;
static SDL_GLContext    s_gl_ctx     = nullptr;
static int              s_w = 1920, s_h = 1080;
static bool             s_running    = true;
static bool             s_reset_tick = false; // discard delta after resume

#ifdef _GAMING_XBOX
// ---------------------------------------------------------------
// GDK PLM (Process Lifetime Management) — Xbox certification
// ---------------------------------------------------------------
// SDL2 uses its Win32 backend (not its GDK backend) because the GDK MSBuild
// platform is detected via VS2022 platform registration rather than a CMake
// toolchain file.  The Win32 backend does not register GDK PLM callbacks, so
// SDL_APP_WILLENTERBACKGROUND may not fire reliably for console PLM events.
//
// We register our own suspend callback directly with the GDK task queue.
// XSuspendResumeAcknowledge() MUST be called within ~2 seconds of the OS
// delivering the suspend notification, or the OS force-terminates the title —
// a hard Xbox certification requirement.
static XTaskQueueHandle            s_plm_queue = nullptr;
static XTaskQueueRegistrationToken s_plm_token = {};

static void CALLBACK plm_suspend_callback(void * /*ctx*/,
                                           XSuspendResumeAcknowledgmentId ackId)
{
    if (s_game) s_game->focus_lost();
    s_reset_tick = true;
    // Acknowledge the suspend event to the OS before the 2-second deadline.
    XSuspendResumeAcknowledge(ackId);
}
#endif // _GAMING_XBOX

int main(int argc, char *argv[])
{
    // Required when SDL_MAIN_HANDLED is defined: tells SDL that the
    // application has initialised itself and SDL_Init can proceed.
    SDL_SetMainReady();

    (void)argc; (void)argv;
    srand((unsigned)time(NULL));

    // Tell SDL2 to use the ANGLE EGL implementation for OpenGL ES.
    // Without this hint SDL2 would try the native WGL path, which is
    // unavailable on Xbox.
    SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");

    // Route SDL log output to a file alongside the exe so errors are
    // visible even when launched without a console (double-click / artifact).
#ifdef _GAMING_DESKTOP
    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_VERBOSE);
    {
        char logPath[MAX_PATH];
        GetModuleFileNameA(NULL, logPath, MAX_PATH);
        char *slash = strrchr(logPath, '\\');
        if (slash) strcpy(slash + 1, "newtonia.log");
        FILE *logFile = fopen(logPath, "w");
        if (logFile) {
            SDL_LogSetOutputFunction([](void *fp, int /*cat*/, SDL_LogPriority /*pri*/, const char *msg) {
                fprintf((FILE *)fp, "%s\n", msg);
                fflush((FILE *)fp);
            }, logFile);
        }
    }
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Newtonia", SDL_GetError(), NULL);
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    // Request an OpenGL ES 2.0 context through ANGLE / EGL.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,   16);

#ifdef _GAMING_XBOX
    // Xbox always renders fullscreen; query the display's native resolution.
    {
        SDL_DisplayMode dm;
        if (SDL_GetCurrentDisplayMode(0, &dm) == 0 && dm.w > 0 && dm.h > 0) {
            s_w = dm.w;
            s_h = dm.h;
        }
    }
#else
    // Desktop: start at a sensible windowed size; the user can resize.
    s_w = 1280;
    s_h = 720;
#endif

    const Uint32 window_flags = SDL_WINDOW_OPENGL
#ifdef _GAMING_XBOX
        | SDL_WINDOW_FULLSCREEN
#else
        | SDL_WINDOW_RESIZABLE
#endif
        ;

    s_window = SDL_CreateWindow("Newtonia",
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                s_w, s_h,
                                window_flags);
    if (!s_window) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Newtonia", SDL_GetError(), NULL);
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    s_gl_ctx = SDL_GL_CreateContext(s_window);
    if (!s_gl_ctx) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Newtonia", SDL_GetError(), s_window);
        SDL_Log("SDL_GL_CreateContext failed: %s", SDL_GetError());
        SDL_DestroyWindow(s_window);
        SDL_Quit();
        return 1;
    }

    SDL_GL_SetSwapInterval(1); // vsync

    gles2_init();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Audio: 48 kHz matches the Xbox audio subsystem's native rate.
    if (Mix_OpenAudio(48000, MIX_DEFAULT_FORMAT, 2, 512) < 0)
        SDL_Log("Mix_OpenAudio failed: %s", Mix_GetError());
    Mix_AllocateChannels(32);

    // Open the first available game controller (the GDK exposes all connected
    // Xbox controllers as SDL_GameController devices).
    SDL_JoystickEventState(SDL_ENABLE);
    SDL_GameController *controller = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            controller = SDL_GameControllerOpen(i);
            if (controller) break;
        }
    }

    load_preferences();

#ifdef _GAMING_XBOX
    // Register the GDK PLM suspend callback.  XTaskQueueDispatchMode_ThreadPool
    // lets the OS fire the callback on a thread-pool thread immediately when
    // the PLM notification arrives, independent of the main-thread game loop.
    if (SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode_ThreadPool,
                                   XTaskQueueDispatchMode_Manual,
                                   &s_plm_queue))) {
        XSuspendResumeRegisterForSuspend(s_plm_queue, nullptr,
                                         plm_suspend_callback, &s_plm_token);
    }
#endif

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

            // Physical / USB keyboard (optional; controllers are primary input).
            case SDL_KEYDOWN: {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE) {
                    if (!s_game->back_pressed()) s_running = false;
                    break;
                }
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

            // GDK lifecycle: the OS can suspend the title at any time
            // (guide button, system overlay, power management).
            // SDL_APP_WILLENTERBACKGROUND / SDL_APP_DIDENTERFOREGROUND map
            // directly onto the GDK PLM (Process Lifetime Management) events.
            case SDL_APP_WILLENTERBACKGROUND:
#ifndef _GAMING_XBOX
                // On Xbox the direct PLM callback (plm_suspend_callback above)
                // handles suspension and sends XSuspendResumeAcknowledge().
                // On Desktop this SDL event fires from Win32 focus loss; handle it.
                s_game->focus_lost();
#endif
                break;
            case SDL_APP_DIDENTERFOREGROUND:
                s_game->focus_gained();
                s_reset_tick = true; // skip catch-up after resume
                break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
                    e.window.event == SDL_WINDOWEVENT_MINIMIZED) {
                    s_game->focus_lost();
                } else if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED ||
                           e.window.event == SDL_WINDOWEVENT_RESTORED) {
                    s_game->focus_gained();
                    s_reset_tick = true;
#ifndef _GAMING_XBOX
                } else if (e.window.event == SDL_WINDOWEVENT_RESIZED) {
                    s_w = e.window.data1;
                    s_h = e.window.data2;
                    s_game->resize(s_w, s_h);
                    Typer::resize(s_w, s_h);
#endif
                }
                break;

            // All controller axis / button events are forwarded to GLShip
            // via StateManager::controller(), which already handles
            // SDL_GameController events for analog sticks and buttons.
            default:
                s_game->controller(e);
                break;
            }
        }

        Uint32 now = SDL_GetTicks();
        if (s_reset_tick) {
            // Discard time accumulated during suspension so the simulation
            // doesn't attempt to catch up on a potentially long gap.
            last_tick    = now;
            s_reset_tick = false;
        }
        int delta = (int)(now - last_tick);
        last_tick = now;

        s_game->tick(delta);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        s_game->draw();

        SDL_GL_SwapWindow(s_window);
    }

    delete s_game;
    Asteroid::free_sounds();
    Typer::cleanup();
    gles2_shutdown();
    Mix_CloseAudio();
    if (controller) SDL_GameControllerClose(controller);
    SDL_GL_DeleteContext(s_gl_ctx);
    SDL_DestroyWindow(s_window);
    SDL_Quit();

#ifdef _GAMING_XBOX
    if (s_plm_queue) {
        XSuspendResumeUnregisterForSuspend(&s_plm_token);
        XTaskQueueCloseHandle(s_plm_queue);
    }
#endif

    return 0;
}

#endif // _GAMING_XBOX || _GAMING_DESKTOP
