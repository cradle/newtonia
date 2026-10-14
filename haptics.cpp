#include "haptics.h"
#include "preferences.h"

#include <SDL.h>
#ifdef __ANDROID__
#include <jni.h>
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace {

struct Feel {
  Uint16 low, high;   // pad motors (0..0xFFFF): low = heavy, high = buzzy
  Uint32 pad_ms;
  int phone_ms;       // device motor
  int amplitude;      // 1..255 where the phone can vary it
};

// Tuned to read as a flick for the small things and a thump for the big
// ones. Phone durations are shorter than the pad's: a phone motor is
// stronger for its size and long buzzes feel like a notification.
const Feel FEEL[Haptics::EVENT_COUNT] = {
  { 0x1800, 0x5000,  60,  18,  90 },  // PICKUP
  { 0x5000, 0x1800,  90,  25, 140 },  // BOOST
  { 0x2800, 0x4800, 160,  40, 160 },  // LEVEL_CLEAR
  { 0xC000, 0xC000, 350, 110, 255 },  // NOVA
  { 0xFFFF, 0x9000, 450, 180, 255 },  // DEATH
};

// One buzz at a time per motor: a weaker event inside a stronger one's
// window is dropped rather than replacing it.
struct Busy {
  Uint32 until = 0;
  int strength = -1;
  bool take(Haptics::Event e, Uint32 ms) {
    Uint32 now = SDL_GetTicks();
    if ((int)e < strength && (Sint32)(until - now) > 0) return false;
    strength = (int)e;
    until = now + ms;
    return true;
  }
};

const char *const NAMES[Haptics::EVENT_COUNT] = {
  "PICKUP", "BOOST", "LEVEL_CLEAR", "NOVA", "DEATH"};

// NEWTONIA_HAPTICS_DEBUG=1: one log line per buzz (logcat tag SDL/APP),
// for checking which events fire without feeling for them.
bool debug_log() {
  static int on = -1;
  if (on < 0) {
    const char *v = SDL_getenv("NEWTONIA_HAPTICS_DEBUG");
    on = (v && v[0] == '1') ? 1 : 0;
  }
  return on == 1;
}

Busy s_phone;
bool s_external = false;

// Pads: a few slots keyed by id is plenty (four seats).
struct PadBusy { PadId id; Busy busy; };
PadBusy s_pads[8] = {
  {PAD_NONE, Busy()}, {PAD_NONE, Busy()}, {PAD_NONE, Busy()}, {PAD_NONE, Busy()},
  {PAD_NONE, Busy()}, {PAD_NONE, Busy()}, {PAD_NONE, Busy()}, {PAD_NONE, Busy()},
};

Busy &pad_busy(PadId id) {
  for (auto &p : s_pads)
    if (p.id == id) return p.busy;
  // Reuse the slot whose buzz ended longest ago.
  PadBusy *pick = &s_pads[0];
  for (auto &p : s_pads)
    if ((Sint32)(p.busy.until - pick->busy.until) < 0) pick = &p;
  pick->id = id;
  pick->busy = Busy();
  return pick->busy;
}

#ifdef __ANDROID__
void android_vibrate(int ms, int amplitude) {
  JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
  jobject activity = (jobject)SDL_AndroidGetActivity();
  if (!env || !activity) return;
  jclass clazz = env->GetObjectClass(activity);
  if (clazz) {
    jmethodID m = env->GetMethodID(clazz, "vibrate", "(II)V");
    if (m) env->CallVoidMethod(activity, m, (jint)ms, (jint)amplitude);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(clazz);
  }
  env->DeleteLocalRef(activity);
}
#endif

}  // namespace

namespace Haptics {

void pad(PadId id, Event e) {
  if (!g_prefs.vibration || id == PAD_NONE || pad_is_steam(id)) return;
  if ((int)e < 0 || e >= EVENT_COUNT) return;
  SDL_GameController *gc = SDL_GameControllerFromInstanceID((SDL_JoystickID)id);
  if (!gc) return;
  const Feel &f = FEEL[e];
  if (!pad_busy(id).take(e, f.pad_ms)) return;
  int rc = SDL_GameControllerRumble(gc, f.low, f.high, f.pad_ms);
  if (debug_log())
    SDL_Log("haptics: %s on pad %d%s", NAMES[e], (int)id,
            rc == 0 ? "" : " (no rumble support)");
}

void phone(Event e) {
  if (!g_prefs.vibration || s_external) return;
  if ((int)e < 0 || e >= EVENT_COUNT) return;
  const Feel &f = FEEL[e];
  if (!s_phone.take(e, (Uint32)f.phone_ms)) return;
  if (debug_log()) SDL_Log("haptics: %s on phone (%d ms)", NAMES[e], f.phone_ms);
#if defined(__ANDROID__)
  android_vibrate(f.phone_ms, f.amplitude);
#elif defined(__EMSCRIPTEN__)
  EM_ASM({ try { if (navigator.vibrate) navigator.vibrate($0); } catch (e) {} },
         f.phone_ms);
#else
  (void)f;
#endif
}

void set_external_display(bool external) { s_external = external; }

}  // namespace Haptics
