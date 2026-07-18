// GDK entry point — Xbox console (_GAMING_XBOX) and GDK Desktop (_GAMING_DESKTOP).
//
// Uses SDL2 with OpenGL ES 2.0 via ANGLE, which translates GLES calls to
// Direct3D 11 — the same path used on Android / Web, avoiding a full graphics
// rewrite.  The ANGLE libraries (libEGL / libGLESv2) come from the
// ANGLE.WindowsStore NuGet package, not the GDK (see xbox/CMakeLists.txt).
//
// Mirrors android_main.cpp: SDL2 event loop, SDL_GameController input,
// focus-lost / focus-gained lifecycle events required by GDK certification.
//
// Platform differences
// --------------------
// _GAMING_XBOX    : fullscreen at native display resolution; no resize events.
//                   Window surface via eglCreateWindowSurface; eglSwapBuffers.
// _GAMING_DESKTOP : 1280×720 resizable window; handles SDL_WINDOWEVENT_RESIZED.
//                   ANGLE.WindowsStore requires IInspectable* for window surfaces,
//                   not a plain HWND, so we render into an EGL Pbuffer and blit
//                   each frame with glReadPixels + GDI StretchDIBits.
//                   biHeight is positive (bottom-up DIB) matching glReadPixels row order.
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
#include <SDL_syswm.h>   // SDL_GetWindowWMInfo / HWND

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

// EGL headers — provided by ANGLE (bundled with the GDK).
// SDL2 compiled via FetchContent on Windows only knows WGL, so the EGL
// context is managed manually for _GAMING_DESKTOP rather than going
// through SDL_GL_CreateContext.
#include <EGL/egl.h>

#include "gles2_compat.h"
#include "state_manager.h"
#include "typer.h"
#include "asteroid.h"
#include "preferences.h"
#include "net_transport.h"
#include "net_signal.h"

#include <cstdlib>
#include <ctime>
#include <windows.h>

#ifdef _GAMING_DESKTOP
#include <vector>
#endif

static StateManager    *s_game       = nullptr;
static SDL_Window      *s_window     = nullptr;
static int              s_w = 1920, s_h = 1080;
static bool             s_running    = true;
static bool             s_reset_tick = false; // discard delta after resume

// Up to two controllers (player 1 / player 2), mirroring glut.cpp.
// SDL only delivers controller events for opened devices, so hot-plugged
// pads must be opened here on SDL_CONTROLLERDEVICEADDED (issue #287).
static SDL_GameController *s_controllers[2]    = { nullptr, nullptr };
static SDL_JoystickID      s_controller_ids[2] = { -1, -1 };

// EGL handles — used on both Desktop and Xbox.
static EGLDisplay s_egl_display = EGL_NO_DISPLAY;
static EGLSurface s_egl_surface = EGL_NO_SURFACE;
static EGLContext s_egl_context = EGL_NO_CONTEXT;
static EGLConfig  s_egl_config  = nullptr; // saved for pbuffer recreation on resize

#ifdef _GAMING_DESKTOP
static HWND s_hwnd       = nullptr;
static bool s_fullscreen = false;
static int  s_pre_fs_w = 1280, s_pre_fs_h = 720;
static int  s_pre_fs_x = 0,    s_pre_fs_y = 0;
static std::vector<unsigned char> s_pixels;

static void toggle_fullscreen()
{
    if (!s_fullscreen) {
        SDL_GetWindowSize(s_window, &s_pre_fs_w, &s_pre_fs_h);
        SDL_GetWindowPosition(s_window, &s_pre_fs_x, &s_pre_fs_y);
        SDL_SetWindowFullscreen(s_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        SDL_ShowCursor(SDL_DISABLE);
        s_fullscreen = true;
    } else {
        SDL_SetWindowFullscreen(s_window, 0);
        SDL_SetWindowSize(s_window, s_pre_fs_w, s_pre_fs_h);
        SDL_SetWindowPosition(s_window, s_pre_fs_x, s_pre_fs_y);
        SDL_ShowCursor(SDL_ENABLE);
        s_fullscreen = false;
    }
    g_prefs.fullscreen = s_fullscreen;
    save_preferences();
}

static void present_pbuffer()
{
    glReadPixels(0, 0, s_w, s_h, GL_RGBA, GL_UNSIGNED_BYTE, s_pixels.data());
    // Swap R and B in-place: OpenGL returns RGBA, GDI expects BGRA.
    unsigned char *p = s_pixels.data();
    for (int i = 0; i < s_w * s_h; ++i, p += 4) {
        unsigned char t = p[0]; p[0] = p[2]; p[2] = t;
    }
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = s_w;
    bmi.bmiHeader.biHeight      = s_h; // positive = bottom-up DIB, matches glReadPixels row order
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    HDC hdc = GetDC(s_hwnd);
    StretchDIBits(hdc, 0, 0, s_w, s_h, 0, 0, s_w, s_h,
                  s_pixels.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(s_hwnd, hdc);
}

static bool recreate_pbuffer()
{
    eglMakeCurrent(s_egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(s_egl_display, s_egl_surface);
    const EGLint attribs[] = { EGL_WIDTH, s_w, EGL_HEIGHT, s_h, EGL_NONE };
    s_egl_surface = eglCreatePbufferSurface(s_egl_display, s_egl_config, attribs);
    if (s_egl_surface == EGL_NO_SURFACE) {
        SDL_Log("eglCreatePbufferSurface (resize) failed: 0x%x", eglGetError());
        return false;
    }
    if (!eglMakeCurrent(s_egl_display, s_egl_surface, s_egl_surface, s_egl_context)) {
        SDL_Log("eglMakeCurrent (resize) failed: 0x%x", eglGetError());
        return false;
    }
    s_pixels.resize((size_t)s_w * s_h * 4);
    return true;
}
#endif // _GAMING_DESKTOP

#ifdef _GAMING_XBOX
// ---------------------------------------------------------------
// GDK PLM (Process Lifetime Management) — Xbox certification
// ---------------------------------------------------------------
static XTaskQueueHandle            s_plm_queue = nullptr;
static XTaskQueueRegistrationToken s_plm_token = {};

static void CALLBACK plm_suspend_callback(void * /*ctx*/,
                                           XSuspendResumeAcknowledgmentId ackId)
{
    if (s_game) s_game->focus_lost();
    s_reset_tick = true;
    XSuspendResumeAcknowledge(ackId);
}
#endif // _GAMING_XBOX

// Map an SDL keycode to the game's key encoding. ASCII keys (< 128) pass
// through; function keys F1..F12 become 129..140 (128 + GLUT_KEY_Fn), matching
// how glut.cpp feeds them and what the bindings expect (e.g. help = F1 = 129).
// Returns 0 for keys the game doesn't use. SDLK_F1..SDLK_F12 are contiguous.
static unsigned char game_key_from_sdl(SDL_Keycode k)
{
    if (k >= SDLK_F1 && k <= SDLK_F12) return (unsigned char)(129 + (k - SDLK_F1));
    // Arrows use desktop GLUT's special-key codes; menus alias them to
    // WASD (State::nav_key).
    switch (k) {
        case SDLK_UP:    return 128 + GLUT_KEY_UP;
        case SDLK_DOWN:  return 128 + GLUT_KEY_DOWN;
        case SDLK_LEFT:  return 128 + GLUT_KEY_LEFT;
        case SDLK_RIGHT: return 128 + GLUT_KEY_RIGHT;
        default: break;
    }
    return (k < 128) ? (unsigned char)k : 0;
}

int main(int argc, char *argv[])
{
    SDL_SetMainReady();

    (void)argc; (void)argv;
    srand((unsigned)time(NULL));

    // Route SDL log output to a file alongside the exe so errors are
    // visible when launched without a console.
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

#ifdef NEWTONIA_NET_RTC
    // Hidden CI/debug hook: run the netplay loopback self-test and exit.
    // Placed before SDL_Init so it works headless (no display needed).
    {
        const char *st = SDL_getenv("NEWTONIA_NET_SELFTEST");
        if (st && st[0] == '1' && st[1] == '\0') {
            SDL_Log("NEWTONIA_NET_SELFTEST: running loopback self-test...");
            bool ok = net_selftest();
            SDL_Log(ok ? "NET SELFTEST PASS" : "NET SELFTEST FAIL");
            return ok ? 0 : 1;
        }
        // M2 room-code relay self-test (needs a live signal server:
        // wrangler dev locally, or NEWTONIA_SIGNAL_URL to point elsewhere).
        const char *ss = SDL_getenv("NEWTONIA_SIGNAL_SELFTEST");
        // (load_preferences runs later in normal startup; the selftest
        // resolves the relay URL now, so honour the INI override here.)
        if (ss && ss[0] == '1' && ss[1] == '\0') {
            load_preferences();
            SDL_Log("NEWTONIA_SIGNAL_SELFTEST: running relay self-test...");
            bool ok = net_signal_selftest();
            SDL_Log(ok ? "SIGNAL SELFTEST PASS" : "SIGNAL SELFTEST FAIL");
            return ok ? 0 : 1;
        }
    }
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Newtonia", SDL_GetError(), NULL);
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    SDL_Log("SDL_Init OK");

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

    // Create window — no SDL_WINDOW_OPENGL.  The EGL context is created
    // manually below using ANGLE directly; SDL2 compiled via FetchContent
    // on Windows only supports WGL and would try to find wglGetProcAddress
    // inside libEGL.dll, which fails.
    SDL_Log("Creating window (%dx%d)...", s_w, s_h);
    const Uint32 window_flags =
#ifdef _GAMING_XBOX
        SDL_WINDOW_FULLSCREEN;
#else
        SDL_WINDOW_RESIZABLE;
#endif

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
    SDL_Log("Window created");

    // ---------------------------------------------------------------
    // EGL / ANGLE context — set up manually.
    // SDL2 is used only for windowing and input; EGL is managed directly
    // so we control the GLES2 context regardless of SDL's GL backend.
    // ---------------------------------------------------------------
    {
        SDL_SysWMinfo wm;
        SDL_VERSION(&wm.version);
        SDL_GetWindowWMInfo(s_window, &wm);
#ifdef _GAMING_XBOX
        EGLNativeWindowType nativeWin = (EGLNativeWindowType)wm.info.win.window;
#else
        s_hwnd = (HWND)wm.info.win.window;
#endif

        s_egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (s_egl_display == EGL_NO_DISPLAY) {
            SDL_Log("eglGetDisplay failed: 0x%x", eglGetError());
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Newtonia", "eglGetDisplay failed", s_window);
            SDL_DestroyWindow(s_window); SDL_Quit(); return 1;
        }

        EGLint major = 0, minor = 0;
        if (!eglInitialize(s_egl_display, &major, &minor)) {
            SDL_Log("eglInitialize failed: 0x%x", eglGetError());
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Newtonia", "eglInitialize failed", s_window);
            SDL_DestroyWindow(s_window); SDL_Quit(); return 1;
        }
        SDL_Log("EGL %d.%d", major, minor);

        static const EGLint cfg_attribs[] = {
            EGL_RED_SIZE,   8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE,  8,
            EGL_DEPTH_SIZE, 16,
#ifdef _GAMING_DESKTOP
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
#endif
            EGL_NONE
        };
        EGLint num_configs = 0;
        SDL_Log("eglChooseConfig...");
        eglChooseConfig(s_egl_display, cfg_attribs, &s_egl_config, 1, &num_configs);
        if (num_configs == 0) {
            SDL_Log("eglChooseConfig: no matching config (0x%x)", eglGetError());
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Newtonia", "eglChooseConfig failed", s_window);
            SDL_DestroyWindow(s_window); SDL_Quit(); return 1;
        }
        SDL_Log("eglChooseConfig OK");

#ifdef _GAMING_XBOX
        SDL_Log("eglCreateWindowSurface...");
        s_egl_surface = eglCreateWindowSurface(s_egl_display, s_egl_config, nativeWin, nullptr);
        if (s_egl_surface == EGL_NO_SURFACE) {
            SDL_Log("eglCreateWindowSurface failed: 0x%x", eglGetError());
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Newtonia", "eglCreateWindowSurface failed", s_window);
            SDL_DestroyWindow(s_window); SDL_Quit(); return 1;
        }
        SDL_Log("eglCreateWindowSurface OK");
#else
        SDL_Log("eglCreatePbufferSurface...");
        {
            const EGLint pbuf_attribs[] = { EGL_WIDTH, s_w, EGL_HEIGHT, s_h, EGL_NONE };
            s_egl_surface = eglCreatePbufferSurface(s_egl_display, s_egl_config, pbuf_attribs);
        }
        if (s_egl_surface == EGL_NO_SURFACE) {
            SDL_Log("eglCreatePbufferSurface failed: 0x%x", eglGetError());
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Newtonia", "eglCreatePbufferSurface failed", s_window);
            SDL_DestroyWindow(s_window); SDL_Quit(); return 1;
        }
        SDL_Log("eglCreatePbufferSurface OK");
        s_pixels.resize((size_t)s_w * s_h * 4);
#endif

        static const EGLint ctx_attribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
        };
        SDL_Log("eglCreateContext...");
        s_egl_context = eglCreateContext(s_egl_display, s_egl_config, EGL_NO_CONTEXT, ctx_attribs);
        if (s_egl_context == EGL_NO_CONTEXT) {
            SDL_Log("eglCreateContext failed: 0x%x", eglGetError());
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Newtonia", "eglCreateContext failed", s_window);
            SDL_DestroyWindow(s_window); SDL_Quit(); return 1;
        }
        SDL_Log("eglCreateContext OK");

        SDL_Log("eglMakeCurrent...");
        if (!eglMakeCurrent(s_egl_display, s_egl_surface, s_egl_surface, s_egl_context)) {
            SDL_Log("eglMakeCurrent failed: 0x%x", eglGetError());
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Newtonia", "eglMakeCurrent failed", s_window);
            SDL_DestroyWindow(s_window); SDL_Quit(); return 1;
        }
#ifdef _GAMING_XBOX
        eglSwapInterval(s_egl_display, 1); // vsync
#endif
    }
    SDL_Log("EGL context active");

    gles2_init();
    SDL_Log("gles2_init OK");

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Audio: 48 kHz matches the Xbox audio subsystem's native rate.
    if (Mix_OpenAudio(48000, MIX_DEFAULT_FORMAT, 2, 512) < 0)
        SDL_Log("Mix_OpenAudio failed: %s", Mix_GetError());
    // 64 channels + 2 reserved for must-hear booms — see glut.cpp.
    Mix_AllocateChannels(64);
    Mix_ReserveChannels(2);

    // Open all (up to 2) game controllers present at startup; hot-plugged
    // ones are opened by SDL_CONTROLLERDEVICEADDED in the event loop.
    SDL_JoystickEventState(SDL_ENABLE);
    SDL_GameControllerEventState(SDL_ENABLE);
    // Controller diagnostics (issue #287): enumerate what SDL sees at boot so
    // newtonia.log pinpoints whether detection, opening, or events fail.
    SDL_Log("Joysticks at startup: %d", SDL_NumJoysticks());
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        const char *name = SDL_JoystickNameForIndex(i);
        SDL_Log("  joystick %d: %s%s", i,
                name ? name : "(unnamed)",
                SDL_IsGameController(i) ? " [gamecontroller]" : " [NOT a gamecontroller]");
        if (!SDL_IsGameController(i)) continue;
        for (int s = 0; s < 2; s++) {
            if (s_controllers[s]) continue;
            s_controllers[s] = SDL_GameControllerOpen(i);
            if (s_controllers[s]) {
                s_controller_ids[s] = SDL_JoystickInstanceID(
                    SDL_GameControllerGetJoystick(s_controllers[s]));
                SDL_Log("  opened controller %d as player %d: %s",
                        i, s + 1, SDL_GameControllerName(s_controllers[s]));
            } else {
                SDL_Log("  SDL_GameControllerOpen(%d) FAILED: %s", i, SDL_GetError());
            }
            break;
        }
    }
    if (!s_controllers[0]) SDL_Log("No controller opened at startup (hot-plug still active)");

    load_preferences();
    SDL_Log("Preferences loaded");

#ifndef _GAMING_XBOX
    if (g_prefs.fullscreen) {
        SDL_SetWindowFullscreen(s_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        SDL_ShowCursor(SDL_DISABLE);
        s_fullscreen = true;
    }
#endif

#ifdef _GAMING_XBOX
    if (SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode_ThreadPool,
                                   XTaskQueueDispatchMode_Manual,
                                   &s_plm_queue))) {
        XSuspendResumeRegisterForSuspend(s_plm_queue, nullptr,
                                         plm_suspend_callback, &s_plm_token);
    }
#endif

    s_game = new StateManager();
    SDL_Log("StateManager created");
    // Register controllers opened before the StateManager existed (glut.cpp
    // does the same); hot-plugged ones are registered by the event loop.
    for (int i = 0; i < 2; i++) {
        if (s_controllers[i]) s_game->controller_added(s_controllers[i]);
    }
    s_game->resize(s_w, s_h);
    Typer::resize(s_w, s_h);
    SDL_Log("Entering main loop");

    Uint32 last_tick = SDL_GetTicks();

    while (s_running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT:
                s_running = false;
                break;

            case SDL_KEYDOWN: {
                if (e.key.repeat) break; // game tracks held state itself; ignore SDL repeats
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE) {
                    if (!s_game->back_pressed()) s_running = false;
                    break;
                }
#ifndef _GAMING_XBOX
                if (k == g_prefs.general_keys.toggle_fullscreen) {
                    toggle_fullscreen();
                    // fall through — game also receives the key, matching glut.cpp
                }
#endif
                unsigned char key = game_key_from_sdl(k);
                if (key) s_game->keyboard(key, 0, 0);
                break;
            }
            case SDL_KEYUP: {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE) break; // handled only via back_pressed() on keydown
#ifndef _GAMING_XBOX
                if (k == g_prefs.general_keys.toggle_fullscreen) break; // not passed to game
#endif
                unsigned char key = game_key_from_sdl(k);
                if (key) s_game->keyboard_up(key, 0, 0);
                break;
            }

            case SDL_APP_WILLENTERBACKGROUND:
#ifndef _GAMING_XBOX
                s_game->focus_lost();
#endif
                break;
            case SDL_APP_DIDENTERFOREGROUND:
                s_game->focus_gained();
                s_reset_tick = true;
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
                    recreate_pbuffer();
#endif
                }
                break;

            // Hot-plug (issue #287): SDL only delivers controller events for
            // OPENED devices, so open into a free player slot here. SDL also
            // queues DEVICEADDED for pads present at init — skip ones the
            // startup scan already opened (same instance id).
            case SDL_CONTROLLERDEVICEADDED: {
                SDL_Log("SDL_CONTROLLERDEVICEADDED (device index %d)", (int)e.cdevice.which);
                SDL_JoystickID add_id = SDL_JoystickGetDeviceInstanceID(e.cdevice.which);
                if (add_id != s_controller_ids[0] && add_id != s_controller_ids[1]) {
                    for (int i = 0; i < 2; i++) {
                        if (s_controllers[i]) continue;
                        s_controllers[i] = SDL_GameControllerOpen(e.cdevice.which);
                        if (s_controllers[i]) {
                            s_controller_ids[i] = SDL_JoystickInstanceID(
                                SDL_GameControllerGetJoystick(s_controllers[i]));
                            SDL_Log("Controller %d connected: %s",
                                    i + 1, SDL_GameControllerName(s_controllers[i]));
                            s_game->controller_added(s_controllers[i]);
                        } else {
                            SDL_Log("SDL_GameControllerOpen failed: %s", SDL_GetError());
                        }
                        break;
                    }
                }
                s_game->controller(e);
                break;
            }
            case SDL_CONTROLLERDEVICEREMOVED: {
                SDL_JoystickID removed_id = (SDL_JoystickID)e.cdevice.which;
                SDL_Log("SDL_CONTROLLERDEVICEREMOVED (instance %d)", (int)removed_id);
                for (int i = 0; i < 2; i++) {
                    if (s_controller_ids[i] == removed_id) {
                        SDL_GameControllerClose(s_controllers[i]);
                        s_controllers[i] = nullptr;
                        s_controller_ids[i] = -1;
                        SDL_Log("Controller %d disconnected", i + 1);
                        s_game->controller_removed(removed_id);
                        break;
                    }
                }
                s_game->controller(e);
                break;
            }
            case SDL_CONTROLLERBUTTONDOWN: {
                static bool first_button_logged = false;
                if (!first_button_logged) {
                    SDL_Log("First SDL_CONTROLLERBUTTONDOWN (button %d) — controller events flowing",
                            (int)e.cbutton.button);
                    first_button_logged = true;
                }
                s_game->controller(e);
                break;
            }

            default:
                s_game->controller(e);
                break;
            }
        }

        Uint32 now = SDL_GetTicks();
        if (s_reset_tick) {
            last_tick    = now;
            s_reset_tick = false;
        }
        int delta = (int)(now - last_tick);
        last_tick = now;

        s_game->tick(delta);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        s_game->draw();

#ifdef _GAMING_XBOX
        eglSwapBuffers(s_egl_display, s_egl_surface);
#else
        present_pbuffer();
#endif
    }

    delete s_game;
    Asteroid::free_sounds();
    Typer::cleanup();
    gles2_shutdown();
    Mix_CloseAudio();
    for (int i = 0; i < 2; i++) {
        if (s_controllers[i] && SDL_GameControllerGetAttached(s_controllers[i]))
            SDL_GameControllerClose(s_controllers[i]);
    }

    eglMakeCurrent(s_egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(s_egl_display, s_egl_context);
    eglDestroySurface(s_egl_display, s_egl_surface);
    eglTerminate(s_egl_display);

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
