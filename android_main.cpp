// Android entry point using SDL2.
// Replaces glut.cpp on Android: handles window creation, the game loop,
// touch → keyboard/controller event mapping, and audio initialisation.

#ifdef __ANDROID__

#include <jni.h>
#include <SDL.h>
#include <SDL_mixer.h>
#include <GLES2/gl2.h>

#include "achievements.h"
#include "gles2_compat.h"
#include "state_manager.h"
#include "touch_controls.h"
#include "typer.h"
#include "asteroid.h"
#include "preferences.h"
#include "invites.h"
#include "world_sound.h"
#include "view/overlay.h"
#ifdef NEWTONIA_NET_RTC
#include "net_signal.h"
#include "net_transport.h"
#endif

#include <atomic>
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

// Display-cutout safe insets: NewtoniaActivity keeps them in static fields
// (populated in onCreate, refreshed in onConfigurationChanged on rotation —
// same JNI pattern as the audio params). Forwarded to the HUD so the
// top-anchored row (LEVEL/score/weapons) clears the camera notch. Called at
// startup and again on every SDL resize, which is what rotation delivers.
static void read_display_safe_insets() {
    int top = 0, bottom = 0, left = 0, right = 0;
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (env && activity) {
        jclass clazz = env->GetObjectClass(activity);
        if (clazz) {
            jfieldID fTop    = env->GetStaticFieldID(clazz, "sSafeInsetTop", "I");
            jfieldID fBottom = env->GetStaticFieldID(clazz, "sSafeInsetBottom", "I");
            jfieldID fLeft   = env->GetStaticFieldID(clazz, "sSafeInsetLeft", "I");
            jfieldID fRight  = env->GetStaticFieldID(clazz, "sSafeInsetRight", "I");
            if (fTop)    top    = env->GetStaticIntField(clazz, fTop);
            if (fBottom) bottom = env->GetStaticIntField(clazz, fBottom);
            if (fLeft)   left   = env->GetStaticIntField(clazz, fLeft);
            if (fRight)  right  = env->GetStaticIntField(clazz, fRight);
            env->DeleteLocalRef(clazz);
        }
        env->DeleteLocalRef(activity);
    }
    Overlay::set_safe_insets((float)top, (float)bottom, (float)left, (float)right);
    SDL_Log("Safe insets: top=%d bottom=%d left=%d right=%d",
            top, bottom, left, right);
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
    // code-alphabet letters) or arm the pause zone. touch_tap on release
    // still fires, which is all the lobby needs (re-summon keyboard etc).
    if (SDL_IsTextInputActive()) return;

    float px = x * (float)s_w;
    float py = y * (float)s_h;

    // Beta skip corner, BEFORE the pause zones (launch with
    // `adb shell am start --es NEWTONIA_BETA 1` to enable).
    if (s_game->debug_skip_corner_tap(x, y)) return;

    // Pause button hit zone: top-right, below score/multiplier (larger than visual circle)
    if(!g_touch_controls.pause_active &&
       tc_dist(px, py, g_touch_controls.pause_cx, g_touch_controls.pause_cy) <= g_touch_controls.pause_hit_radius) {
        g_touch_controls.pause_active = true;
        g_touch_controls.pause_finger = id;
        s_game->keyboard('\r', 0, 0);
        return;
    }

    // One-handed mode: every remaining finger belongs to the shared
    // gesture layer (touch_controls.cpp) — the whole screen is the
    // joystick and taps/long-presses fire. The invisible centre pause
    // zone below is deliberately skipped: it sits exactly where the
    // one-hand stick now rests, and the visible top-right button above
    // stays the pause control.
    if(touch_one_handed()) {
        touch_one_hand_down(s_game, id, px, py, x, y);
        return;
    }

    // Centre-screen pause zone (invisible convenience area; the visible
    // top-right button is the primary control). Deliberately small: the
    // floating joystick claims the whole left half, and in narrow portrait
    // a left thumb naturally strays past 0.30 of the width — the old
    // 0.30..0.70 x 0.25..0.75 box paused mid-manoeuvre (field report
    // 2026-08-03). The bottom edge also stays clear of the joystick hint
    // at 0.75h.
    if(!g_touch_controls.pause_active &&
       x >= 0.38f && x <= 0.62f && y >= 0.30f && y <= 0.60f) {
        g_touch_controls.pause_active = true;
        g_touch_controls.pause_finger = id;
        s_game->keyboard('\r', 0, 0);
        return;
    }

    // HANDEDNESS LEFT mirrors the two-hand layout: the stick claims the
    // RIGHT half and the buttons the left. The circle centres are already
    // mirrored by touch_controls_resize, so the geometry hit tests below
    // need no change — only this half split does.
    bool stick_half = touch_layout_mirrored() ? (x >= 0.5f) : (x < 0.5f);
    if(stick_half) {
        // ---- Stick half: virtual joystick (floating base) ----
        g_touch_controls.joy_cx     = px;
        g_touch_controls.joy_cy     = py;
        g_touch_controls.joy_nx     = 0.0f;
        g_touch_controls.joy_ny     = 0.0f;
        g_touch_controls.joy_active = true;
        g_touch_controls.joy_finger = id;
        // '\r' is ignored during gameplay but lets any tap start from the menu
        s_game->keyboard('\r', 0, 0);
    } else {
        // ---- Buttons half ----
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
        } else if(!g_touch_controls.boost_pressed &&
                  tc_dist(px, py,
                          g_touch_controls.boost_cx,
                          g_touch_controls.boost_cy) <= g_touch_controls.boost_hit_radius) {
            // Presses during the cooldown land and no-op in Ship::boost();
            // the overlay's dimmed circle is the feedback.
            g_touch_controls.boost_pressed = true;
            g_touch_controls.boost_finger  = id;
            s_game->keyboard('e', 0, 0);
        }
        // Touches that don't hit a button are silently ignored.
    }
}

// Send the app to the background (Home), preserving state — the modern
// Android root-Back behaviour (Menu::back_pressed on Android calls this instead
// of raising a quit dialog). moveTaskToBack keeps the task alive so a relaunch
// resumes instantly; focus_lost() has already auto-saved. Plain C++ linkage to
// match the forward declaration in menu.cpp.
void app_move_to_background() {
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (!env || !activity) return;
    jclass cls = env->GetObjectClass(activity);
    jmethodID mid = cls ? env->GetMethodID(cls, "moveTaskToBack", "(Z)Z") : NULL;
    if (mid) env->CallBooleanMethod(activity, mid, JNI_TRUE);
    if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
    if (cls) env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
}

// OS share sheet via ACTION_SEND chooser (see net_transport.h seam).
bool net_share_available() { return true; }
void net_share_text(const std::string &text) {
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (!env || !activity) return;

    jclass intent_cls = env->FindClass("android/content/Intent");
    jstring action = env->NewStringUTF("android.intent.action.SEND");
    jobject intent = env->NewObject(
        intent_cls, env->GetMethodID(intent_cls, "<init>", "(Ljava/lang/String;)V"),
        action);
    jstring mime = env->NewStringUTF("text/plain");
    env->CallObjectMethod(
        intent, env->GetMethodID(intent_cls, "setType",
                                 "(Ljava/lang/String;)Landroid/content/Intent;"),
        mime);
    jstring extra_key = env->NewStringUTF("android.intent.extra.TEXT");
    jstring extra_val = env->NewStringUTF(text.c_str());
    env->CallObjectMethod(
        intent, env->GetMethodID(intent_cls, "putExtra",
            "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;"),
        extra_key, extra_val);

    jstring title = env->NewStringUTF("Share room code");
    jobject chooser = env->CallStaticObjectMethod(
        intent_cls, env->GetStaticMethodID(intent_cls, "createChooser",
            "(Landroid/content/Intent;Ljava/lang/CharSequence;)Landroid/content/Intent;"),
        intent, title);

    jclass activity_cls = env->GetObjectClass(activity);
    env->CallVoidMethod(
        activity, env->GetMethodID(activity_cls, "startActivity",
                                   "(Landroid/content/Intent;)V"),
        chooser);

    env->DeleteLocalRef(intent_cls); env->DeleteLocalRef(action);
    env->DeleteLocalRef(intent);     env->DeleteLocalRef(mime);
    env->DeleteLocalRef(extra_key);  env->DeleteLocalRef(extra_val);
    env->DeleteLocalRef(title);      env->DeleteLocalRef(chooser);
    env->DeleteLocalRef(activity_cls); env->DeleteLocalRef(activity);
}

// Co-op join link (App Link). NewtoniaActivity extracts the ?code= from a
// tapped https://newtonia.metonymous.com/join?code=XXXX intent and calls this
// (cold launch from onCreate, warm from onNewIntent). The code flows into the
// shared invite layer; Menu::tick's poll_accepted_invite jumps into the lobby
// as a joiner — the same handoff Steam / the web ?code= path use. Called on
// the Android UI thread, so note_accepted's mutex matters.
extern "C" JNIEXPORT void JNICALL
Java_org_newtonia_NewtoniaActivity_nativeAcceptInvite(JNIEnv *env, jclass, jstring code) {
    if (!code) return;
    const char *c = env->GetStringUTFChars(code, NULL);
    if (c) {
        Invites::note_accepted(c);
        env->ReleaseStringUTFChars(code, c);
    }
}

// Soft-keyboard coverage for the lobby's CodeEntry layout: NewtoniaActivity's
// global-layout listener reports how much of the window the soft keyboard
// covers (0 = hidden), and net_lobby.cpp lifts the LAN "TAP TO JOIN" bands
// clear of it (the fixed band positions measured on one phone drowned under
// taller keyboards). Written on the Android UI thread, read on the game
// thread — hence the atomic.
static std::atomic<float> s_keyboard_fraction(0.0f);

extern "C" JNIEXPORT void JNICALL
Java_org_newtonia_NewtoniaActivity_nativeKeyboardFraction(JNIEnv *, jclass, jfloat f) {
    s_keyboard_fraction.store((float)f, std::memory_order_relaxed);
}

// Plain C++ linkage on purpose — net_lobby.cpp declares it identically
// (a C/C++ linkage mismatch across TUs is a known NDK-link bite, see
// CLAUDE.md's Android build notes).
float android_keyboard_cover_fraction() {
    return s_keyboard_fraction.load(std::memory_order_relaxed);
}

static void finger_up(SDL_FingerID id, float x, float y) {
    // Forward tap position on finger-up so menu selections fire on release, not press
    s_game->touch_tap(x, y);

    if(g_touch_controls.pause_active && g_touch_controls.pause_finger == id) {
        g_touch_controls.pause_active = false;
        s_game->keyboard_up('p', 0, 0);
        return;
    }
    // One-handed mode: the gesture layer owns its fingers (joystick
    // release, tap fire). A finger it never tracked — zoom-zone or
    // overflow — falls through to the legacy paths below, which also
    // cleanly release a press begun before the options toggle flipped
    // the mode mid-menu.
    if(touch_one_handed() && touch_one_hand_up(s_game, id)) return;
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
    if(g_touch_controls.boost_pressed && g_touch_controls.boost_finger == id) {
        g_touch_controls.boost_pressed = false;
        s_game->keyboard_up('e', 0, 0);
        return;
    }
    // Legacy: release '\r' (sent without finger tracking; just always release)
    s_game->keyboard_up('\r', 0, 0);
}

static void finger_motion(SDL_FingerID id, float x, float y) {
    float px = x * (float)s_w;
    float py = y * (float)s_h;
    if(touch_one_handed()) {
        touch_one_hand_motion(id, px, py);
        return;
    }
    if(!g_touch_controls.joy_active || g_touch_controls.joy_finger != id)
        return;
    update_joystick_nub(px, py);
}

// ============================================================
#if defined(PLAY_GAMES_BUILD)
// Netplay identity backend warm hook (play_games_identity.cpp). Declared at
// file scope: a linkage-specification (extern "C") is only legal at namespace
// scope, never inside a function body. extern "C" so the name matches the
// backend's definition (both give it C linkage).
extern "C" void net_android_identity_init();
#endif

// SDL assertions must never show a message box on Android. The default
// handler's box (SDLActivity.messageboxShowMessageBox) blocks the calling
// thread in wait() until a dialog button is pressed — but SDL delivers some
// callbacks ON the UI thread (e.g. SDLAudioManager's audio-device hotplug),
// and a box shown from there waits on the very looper that must render the
// dialog and deliver the click. The dialog never appears, the UI thread is
// wedged for good, and every later tap/focus event ANRs ("Waited 10000ms
// for FocusEvent") while the game thread renders on — hit in the field when
// a Bluetooth audio sink connected on the menu (debug APKs only: release
// builds compile SDL_assert out). Log and carry on instead; the assertion
// text still lands in logcat, where it's actually diagnosable.
static SDL_AssertState android_assert_handler(const SDL_AssertData *data,
                                              void *userdata) {
    (void)userdata;
    SDL_Log("SDL assertion '%s' failed at %s:%d (%s) - ignoring",
            data->condition, data->filename, data->linenum, data->function);
    return SDL_ASSERTION_ALWAYS_IGNORE;
}

// SDL2 main
// ============================================================
extern "C" int SDL_main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    SDL_SetAssertionHandler(android_assert_handler, NULL);  // before any SDL call that can assert
    s_running = true;  // reset in case process was kept alive after a previous quit
    srand(time(NULL));

    // Let SDL2 auto-select the best audio backend: AAudio on API 26+ (which
    // honours SDL_HINT_AUDIO_DEVICE_STREAM_ROLE="game" →
    // AAUDIO_PERFORMANCE_MODE_LOW_LATENCY), falling back to OpenSL ES on
    // older devices.
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_STREAM_ROLE, "game");

    // Initialise SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    // SDL2 starts with text input ACTIVE by default; anything gated on
    // SDL_IsTextInputActive() (the finger_down key-synthesis guard) would
    // otherwise be stuck on forever. Only the lobby's code entry turns it
    // back on, via SDL_StartTextInput.
    SDL_StopTextInput();
    SDL_Log("Audio driver in use: %s", SDL_GetCurrentAudioDriver());

#ifdef NEWTONIA_NET_RTC
    // Hidden CI/debug hooks, the Android twins of glut.cpp's. Reached the
    // same way every other NEWTONIA_* var is on this platform:
    //   adb shell am start -S -n org.newtonia/.NewtoniaActivity
    //     --es NEWTONIA_SIGNAL_SELFTEST 1
    // (one line; no continuation here — a trailing backslash would splice
    // the next line into this comment, which -Wcomment rightly flags)
    // (NewtoniaActivity.applyEnvExtras Os.setenvs the extras before native
    // start, so SDL_getenv sees them.)
    //
    // Placed here rather than at the top of SDL_main because both need SDL
    // up — the CA bundle is written to SDL_GetPrefPath — but before the
    // window and GL context, which neither test needs.
    //
    // The VERDICT IS THE LOG LINE, not the exit status: an Android app's
    // exit code does not reach adb, so the strings below are the contract
    // (`adb logcat -s SDL/APP`), and they are byte-identical to the ones
    // the desktop and iOS hooks print so one grep covers every platform.
    // This is why the TLS pass on Android stopped being drive-the-real-
    // -feature-and-watch (TESTING.md).
    {
        const char *st = SDL_getenv("NEWTONIA_NET_SELFTEST");
        const char *ss_env = SDL_getenv("NEWTONIA_SIGNAL_SELFTEST");
        const bool want_net = st && st[0] == '1' && st[1] == '\0';
        const bool want_signal = ss_env && ss_env[0] == '1' && ss_env[1] == '\0';
        // Disarm BOTH before running EITHER, and read the flags above
        // first because unsetenv invalidates those pointers.
        //
        // applyEnvExtras sets these process-wide, and returning from
        // SDL_main finishes the Activity without necessarily ending the
        // PROCESS — the s_running reset at the top of this function exists
        // because SDL_main is re-entered on a cached one. Left armed, the
        // next launch would run a selftest and close the app instead of
        // starting the game. Both vars, because whichever branch runs
        // returns before the other is reached.
        //
        // Belt and braces as it turns out: relaunching after a selftest,
        // including from Recents where the task's retained intent could
        // have replayed the extras, starts the game normally on a real
        // device (TESTING.md). Kept because it costs nothing and the
        // cached-process re-entry is the case this file can actually see.
        if (want_net || want_signal) {
            unsetenv("NEWTONIA_NET_SELFTEST");
            unsetenv("NEWTONIA_SIGNAL_SELFTEST");
        }
        if (want_net) {
            // Can run for MINUTES on a bad path — 3 attempts, each waiting
            // up to 30 s a side — with no window and no event loop yet, so
            // the screen is black and backing out ANRs: SDLActivity's
            // onDestroy joins this thread from the UI thread, and this
            // thread is inside the test. Acceptable for a hook no shipped
            // launch sets, and TESTING says to let it finish; the signal
            // hook below is the ~20 s one.
            SDL_Log("NEWTONIA_NET_SELFTEST: running loopback self-test...");
            bool ok = net_selftest();
            SDL_Log("%s", ok ? "NET SELFTEST PASS" : "NET SELFTEST FAIL");
            SDL_Quit();
            return ok ? 0 : 1;
        }
        // The relay round trip, which on this platform is the whole TLS
        // trust story: MbedTLS reaches no system store, so a handshake that
        // completes proves the roots in net_ca_bundle.cpp wrote, parsed and
        // verified Cloudflare's chain.
        if (want_signal) {
            load_preferences();  // net_signal_url() honours the INI override
            SDL_Log("NEWTONIA_SIGNAL_SELFTEST: running relay self-test...");
            bool ok = net_signal_selftest();
            SDL_Log("%s", ok ? "SIGNAL SELFTEST PASS" : "SIGNAL SELFTEST FAIL");
            SDL_Quit();
            return ok ? 0 : 1;
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

    // SDL_WINDOW_RESIZABLE is what allows device rotation: without it,
    // SDLActivity.setOrientation() locks the activity to the launch
    // orientation via setRequestedOrientation(SENSOR_LANDSCAPE/PORTRAIT),
    // overriding the manifest (which deliberately has no orientation lock).
    // With it, SDL requests UNSPECIFIED and rotation arrives as a plain
    // resize (manifest configChanges keeps the activity alive).
    s_window = SDL_CreateWindow("Newtonia",
                                SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                s_w, s_h,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN |
                                SDL_WINDOW_RESIZABLE);
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

    // Read the device's native audio parameters from NewtoniaActivity via JNI.
    // AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE and PROPERTY_OUTPUT_FRAMES_PER_BUFFER
    // give the primary output stream's optimal values; matching them avoids
    // resampling and achieves the lowest possible round-trip latency.
    int audio_rate   = 48000;
    int audio_frames = 512;
    {
        JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
        jobject activity = (jobject)SDL_AndroidGetActivity();
        if (env && activity) {
            jclass clazz = env->GetObjectClass(activity);
            if (clazz) {
                jfieldID fRate = env->GetStaticFieldID(clazz, "sOptimalSampleRate", "I");
                jfieldID fBuf  = env->GetStaticFieldID(clazz, "sOptimalFramesPerBuffer", "I");
                if (fRate) audio_rate   = env->GetStaticIntField(clazz, fRate);
                if (fBuf)  audio_frames = env->GetStaticIntField(clazz, fBuf);
                env->DeleteLocalRef(clazz);
            }
            env->DeleteLocalRef(activity);
        }
        SDL_Log("Optimal audio params: rate=%d frames=%d", audio_rate, audio_frames);
    }

    // Open the mixer at the device's native rate and buffer size.
    // Do NOT pass SDL_AUDIO_ALLOW_SAMPLES_CHANGE: that lets Android override
    // our buffer size with a much larger one (often 2048–4096 samples), which
    // is the main source of audible sound lag.
    if (Mix_OpenAudioDevice(audio_rate, MIX_DEFAULT_FORMAT, 2, audio_frames,
                            NULL,
                            SDL_AUDIO_ALLOW_FREQUENCY_CHANGE) < 0) {
        SDL_Log("Mix_OpenAudioDevice failed: %s", Mix_GetError());
    } else {
        int freq; Uint16 fmt; int chans;
        Mix_QuerySpec(&freq, &fmt, &chans);
        SDL_Log("Mix opened: %d Hz, fmt=0x%x, channels=%d, chunk=%d",
                freq, fmt, chans, audio_frames);
    }
    // 64 channels; reserved: 2 for must-hear booms + WorldSound's
    // per-channel-volume pool — see glut.cpp / world_sound.h.
    Mix_AllocateChannels(128);
    Mix_ReserveChannels(WorldSound::FIRST_CHANNEL + WorldSound::POOL);

    // Pre-warm the audio pipeline so the first real sound plays without delay.
    {
        Uint8 silence[4] = {0, 0, 0, 0};
        Mix_Chunk warm = {0, silence, sizeof(silence), 0};
        Mix_PlayChannel(-1, &warm, 0);
    }

    // Game controller (Android may have a physical gamepad via USB/BT)
    SDL_JoystickEventState(SDL_ENABLE);
    SDL_GameController *controller = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            controller = SDL_GameControllerOpen(i);
            if (controller) break;
        }
    }

    // Play Games achievements backend: needs the SDL activity via JNI (so
    // after SDL_Init) and must precede the state machine, whose constructors
    // can already fire earns from a resumed save.
    Achievements::init();

#if defined(PLAY_GAMES_BUILD)
    // Netplay identity backend (NETPLAY.md V2): pre-warm the Play Games player
    // name so it's cached before the lobby builds net_local_identity(). Runs
    // AFTER Achievements::init(), which brings up the Play Games SDK +
    // automatic sign-in this backend piggybacks on. (Declared at file scope
    // above — extern "C" can't appear inside a function body.)
    net_android_identity_init();
#endif

    // Load user preferences before creating the state machine so that GLShip
    // constructors can read them (e.g. rotate_view).
    load_preferences();

    // Create the game state machine
    s_game = new StateManager();
    s_game->resize(s_w, s_h);
    Typer::resize(s_w, s_h);
    // Insets before touch layout — the pause button clears the inset-shifted
    // HUD row, so touch_controls_resize needs the cutout inset already known.
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

            // Keyboard: key events are the ONE source of typed characters.
            // Soft keyboards deliver text two ways — commitText (which SDL
            // turns into synthesized KEYDOWN/KEYUP per char *and* an
            // SDL_TEXTINPUT) and real IME KeyEvents (KEYDOWN/KEYUP only, no
            // TEXTINPUT). GBoard mixes both, so feeding from TEXTINPUT
            // drops the KeyEvent-delivered letters, and feeding from both
            // doubles the commitText ones. KEYDOWN fires exactly once per
            // character on every path (and covers Bluetooth keyboards).
            case SDL_TEXTINPUT:
                break;
            case SDL_KEYDOWN: {
                if (e.key.repeat) break; // game tracks held state itself; ignore SDL repeats
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_AC_BACK || k == SDLK_ESCAPE) {
                    if (!s_game->back_pressed()) s_running = false;
                    break;
                }
                unsigned char key = (k < 128) ? (unsigned char)k : 0;
                // Arrows (hardware keyboards / adb) use desktop GLUT's
                // special-key codes; menus alias them to WASD (State::nav_key).
                if (!key) switch (k) {
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
                    case SDLK_UP:    key = 128 + GLUT_KEY_UP;    break;
                    case SDLK_DOWN:  key = 128 + GLUT_KEY_DOWN;  break;
                    case SDLK_LEFT:  key = 128 + GLUT_KEY_LEFT;  break;
                    case SDLK_RIGHT: key = 128 + GLUT_KEY_RIGHT; break;
                    default: break;
                }
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
                } else if(e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    // Rotation / freeform-window resize (the manifest keeps
                    // configChanges=orientation|screenSize, so this event is
                    // the ONLY notification — the activity is not recreated).
                    // s_w/s_h also feed the finger_* normalized->pixel maps,
                    // so without this the viewport, text scale, and every
                    // touch zone stay in the old orientation's geometry.
                    // SIZE_CHANGED (not RESIZED) fires for system-driven
                    // changes like rotation as well as user resizes.
                    touch_controls_reset(s_game);
                    s_w = e.window.data1;
                    s_h = e.window.data2;
                    s_game->resize(s_w, s_h);
                    Typer::resize(s_w, s_h);
                    // Cutout insets rotate with the display — a portrait top
                    // notch becomes a landscape side one. Read them before
                    // the touch layout, which places the pause button below
                    // the inset-shifted HUD row.
                    read_display_safe_insets();
                    touch_controls_resize(s_w, s_h);
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

        // One-hand long-press watchdog + deferred fire-key releases.
        // Unconditional on purpose: a safe no-op in two-hand mode, and a
        // pending release must still land if the pref just flipped.
        touch_one_hand_tick(s_game);

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

#endif // __ANDROID__
