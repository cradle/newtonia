// Steam Input — the physical pad behind a Steam-emulated controller (see
// steam_build.h and pad_style.h). Under Steam Input every configured pad
// reaches SDL as a virtual Xbox 360 controller, so SDL's own type query
// labels a DualSense with letters; ISteamInput knows what the player is
// actually holding — and, through GetActionOriginFromXboxOrigin, which
// physical control the player's layout puts on each emulated button, so
// the hints follow a remap. Nothing else of Steam Input is used — action
// sets and glyph PNGs stay out, and every pad still drives the game
// through SDL's GameController events.
// Compiled in every build; the non-Steam branch reports "unavailable".

#include <SDL.h>

#include "steam_build.h"

#ifdef STEAM_BUILD

#include <cstdio>
#include <cstring>

namespace {
bool g_input_ready = false;

// NEWTONIA_TRACE=1 (stderr) or =/absolute/path (appended to that file):
// diagnostics for the origin lookup — which of the three Steam calls says
// no when a remap doesn't reach the hints. Same switch as glut.cpp's
// startup trace.
FILE *trace_out() {
  static FILE *out = NULL;
  static bool decided = false;
  if (!decided) {
    decided = true;
    const char *t = SDL_getenv("NEWTONIA_TRACE");
    if (t && t[0] == '/') out = fopen(t, "a");
    else if (t) out = stderr;
  }
  return out;
}
bool trace_on() { return trace_out() != NULL; }
#define TRACE(...) do { FILE *o_ = trace_out(); if (o_) { fprintf(o_, __VA_ARGS__); fflush(o_); } } while (0)

// Configuration-loaded watcher: Steam posts SteamInputConfigurationLoaded_t
// when a layout loads or the player edits one; the counter it bumps is
// what pad_style's label cache compares against. Lazily constructed like
// the keyboard watcher (steam_keyboard.cpp), leaked on purpose.
class ConfigWatch {
 public:
  ConfigWatch() : loaded_cb_(this, &ConfigWatch::on_loaded) {}
  unsigned generation() const { return generation_; }

 private:
  void on_loaded(SteamInputConfigurationLoaded_t *) { generation_++; }
  unsigned generation_ = 1;
  CCallback<ConfigWatch, SteamInputConfigurationLoaded_t> loaded_cb_;
};

ConfigWatch &config_watch() {
  static ConfigWatch *w = new ConfigWatch();
  return *w;
}

// Handle 0 = the SDL build has no handle API (pre-2.30): with exactly
// ONE pad on the Steam side there is no ambiguity about which one this
// is. Two or more and we can't tell — the caller falls back to SDL.
InputHandle_t resolve_handle(uint64_t steam_handle) {
  if (steam_handle != 0) return (InputHandle_t)steam_handle;
  InputHandle_t handles[STEAM_INPUT_MAX_COUNT];
  int n = SteamInput()->GetConnectedControllers(handles);
  return n == 1 ? handles[0] : 0;
}

// SDL button <-> the emulated Xbox output Steam calls it <-> the Xbox 360
// origin TranslateActionOrigin folds any physical control back onto.
// Buttons the hints never name (guide, touchpad, paddles) have no row.
struct XboxRow { int sdl_button; EXboxOrigin xbox; EInputActionOrigin x360; };
const XboxRow k_rows[] = {
  { SDL_CONTROLLER_BUTTON_A,             k_EXboxOrigin_A,               k_EInputActionOrigin_XBox360_A },
  { SDL_CONTROLLER_BUTTON_B,             k_EXboxOrigin_B,               k_EInputActionOrigin_XBox360_B },
  { SDL_CONTROLLER_BUTTON_X,             k_EXboxOrigin_X,               k_EInputActionOrigin_XBox360_X },
  { SDL_CONTROLLER_BUTTON_Y,             k_EXboxOrigin_Y,               k_EInputActionOrigin_XBox360_Y },
  { SDL_CONTROLLER_BUTTON_BACK,          k_EXboxOrigin_View,            k_EInputActionOrigin_XBox360_Back },
  { SDL_CONTROLLER_BUTTON_START,         k_EXboxOrigin_Menu,            k_EInputActionOrigin_XBox360_Start },
  { SDL_CONTROLLER_BUTTON_LEFTSTICK,     k_EXboxOrigin_LeftStick_Click, k_EInputActionOrigin_XBox360_LeftStick_Click },
  { SDL_CONTROLLER_BUTTON_RIGHTSTICK,    k_EXboxOrigin_RightStick_Click,k_EInputActionOrigin_XBox360_RightStick_Click },
  { SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  k_EXboxOrigin_LeftBumper,      k_EInputActionOrigin_XBox360_LeftBumper },
  { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, k_EXboxOrigin_RightBumper,     k_EInputActionOrigin_XBox360_RightBumper },
  { SDL_CONTROLLER_BUTTON_DPAD_UP,       k_EXboxOrigin_DPad_North,      k_EInputActionOrigin_XBox360_DPad_North },
  { SDL_CONTROLLER_BUTTON_DPAD_DOWN,     k_EXboxOrigin_DPad_South,      k_EInputActionOrigin_XBox360_DPad_South },
  { SDL_CONTROLLER_BUTTON_DPAD_LEFT,     k_EXboxOrigin_DPad_West,       k_EInputActionOrigin_XBox360_DPad_West },
  { SDL_CONTROLLER_BUTTON_DPAD_RIGHT,    k_EXboxOrigin_DPad_East,       k_EInputActionOrigin_XBox360_DPad_East },
};
const int k_row_count = (int)(sizeof(k_rows) / sizeof(k_rows[0]));

const XboxRow *row_for_sdl(int sdl_button) {
  for (int i = 0; i < k_row_count; i++)
    if (k_rows[i].sdl_button == sdl_button) return &k_rows[i];
  return NULL;
}
int sdl_for_x360(EInputActionOrigin o) {
  for (int i = 0; i < k_row_count; i++)
    if (k_rows[i].x360 == o) return k_rows[i].sdl_button;
  return -1;
}

SteamPadKind kind_from_type(ESteamInputType t) {
  switch (t) {
    case k_ESteamInputType_PS3Controller:
    case k_ESteamInputType_PS4Controller:   return STEAM_PAD_PS4;
    case k_ESteamInputType_PS5Controller:   return STEAM_PAD_PS5;
    case k_ESteamInputType_Unknown:         return STEAM_PAD_UNKNOWN;
    default:                                return STEAM_PAD_OTHER;
  }
}
}  // namespace

void steam_input_init() {
  // NEWTONIA_NO_STEAM_INPUT=1: leave Steam Input uninitialised (hints fall
  // back to SDL's pad type) — the second bisection step after
  // NEWTONIA_NO_STEAM, isolating this interface from the rest of the API.
  if (SDL_getenv("NEWTONIA_NO_STEAM_INPUT")) return;
  // bExplicitlyCallRunFrame = false: SteamAPI_RunCallbacks (pumped every
  // tick by steam_run_callbacks) drives RunFrame for us.
  g_input_ready = SteamInput() && SteamInput()->Init(false);
  if (g_input_ready) config_watch();  // register before the first pump
  TRACE("trace: steam input: interface %s, Init -> %d\n",
        SteamInput() ? "present" : "NULL", (int)g_input_ready);
}

void steam_input_shutdown() {
  if (g_input_ready && SteamInput()) SteamInput()->Shutdown();
  g_input_ready = false;
}

bool steam_input_available() { return g_input_ready; }

SteamPadKind steam_pad_kind(uint64_t steam_handle) {
  if (!g_input_ready || !SteamInput()) return STEAM_PAD_UNKNOWN;
  InputHandle_t h = resolve_handle(steam_handle);
  if (h == 0) return STEAM_PAD_UNKNOWN;
  return kind_from_type(SteamInput()->GetInputTypeForHandle(h));
}

bool steam_pad_origin(uint64_t steam_handle, int sdl_button,
                      int *position_out, char *text_out, size_t text_n) {
  if (!g_input_ready || !SteamInput()) return false;
  const XboxRow *row = row_for_sdl(sdl_button);
  if (!row) return false;
  InputHandle_t h = resolve_handle(steam_handle);
  if (h == 0) {
    if (trace_on() && sdl_button == SDL_CONTROLLER_BUTTON_A) {
      InputHandle_t hs[STEAM_INPUT_MAX_COUNT];
      TRACE("trace: steam origin: no handle (sdl gave %llu, steam lists %d pads)\n",
            (unsigned long long)steam_handle, SteamInput()->GetConnectedControllers(hs));
    }
    return false;
  }
  // The physical control the player's layout binds to this Xbox output —
  // None when Steam Input isn't driving the pad, or nothing is bound.
  EInputActionOrigin physical =
      SteamInput()->GetActionOriginFromXboxOrigin(h, row->xbox);
  // Fold it onto an Xbox 360 position: PS4_Circle -> XBox360_B, and the
  // label table then says "circle" in the pad's vocabulary.
  EInputActionOrigin x360 = physical == k_EInputActionOrigin_None
      ? k_EInputActionOrigin_None
      : SteamInput()->TranslateActionOrigin(k_ESteamInputType_XBox360Controller, physical);
  int pos = sdl_for_x360(x360);
  if (sdl_button == SDL_CONTROLLER_BUTTON_A)
    TRACE("trace: steam origin: handle %llu type %d: A -> physical %d (%s) -> x360 %d -> sdl %d\n",
          (unsigned long long)h, (int)SteamInput()->GetInputTypeForHandle(h),
          (int)physical, physical == k_EInputActionOrigin_None ? "none" : SteamInput()->GetStringForActionOrigin(physical),
          (int)x360, pos);
  if (physical == k_EInputActionOrigin_None) return false;
  *position_out = pos;
  if (text_out && text_n) text_out[0] = '\0';
  if (pos < 0 && text_out && text_n) {
    // No 360 equivalent (a back paddle, a trackpad click): Steam's own
    // localized name for the control is the best label there is.
    const char *name = SteamInput()->GetStringForActionOrigin(physical);
    if (!name || !name[0]) return false;
    strncpy(text_out, name, text_n - 1);
    text_out[text_n - 1] = '\0';
  }
  return true;
}

unsigned steam_input_config_generation() {
  return g_input_ready ? config_watch().generation() : 0;
}

#else  // !STEAM_BUILD

void steam_input_init() {}
void steam_input_shutdown() {}
bool steam_input_available() { return false; }
SteamPadKind steam_pad_kind(uint64_t steam_handle) {
  (void)steam_handle;
  return STEAM_PAD_UNKNOWN;
}
bool steam_pad_origin(uint64_t steam_handle, int sdl_button,
                      int *position_out, char *text_out, size_t text_n) {
  (void)steam_handle; (void)sdl_button; (void)position_out;
  (void)text_out; (void)text_n;
  return false;
}
unsigned steam_input_config_generation() { return 0; }

#endif
