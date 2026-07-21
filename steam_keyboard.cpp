// Steam Deck floating-keyboard dismissal watcher (see steam_build.h).
// The lobby used to learn about a dismissed keyboard only from the NEXT
// controller event reaching it (the keyboard consumes pad input while
// up), so the picker reappeared one input late (Glenn, Deck beta test).
// Steamworks posts FloatingGamepadTextInputDismissed_t the moment the
// keyboard closes — poll it one-shot style like the invite seam.
// Compiled in every build; the non-Steam branch is a constant false.

#include <SDL.h>

#include "steam_build.h"

#ifdef STEAM_BUILD

namespace {

class KeyboardWatch {
 public:
  KeyboardWatch() : dismiss_cb_(this, &KeyboardWatch::on_dismissed) {}
  bool take_dismissed() {
    bool d = dismissed_;
    dismissed_ = false;
    return d;
  }

 private:
  void on_dismissed(FloatingGamepadTextInputDismissed_t *) {
    dismissed_ = true;
  }
  bool dismissed_ = false;
  CCallback<KeyboardWatch, FloatingGamepadTextInputDismissed_t> dismiss_cb_;
};

// Lazily constructed on the first poll — the lobby polls every tick, so
// registration happens well before any keyboard is summoned. Leaked on
// purpose (module lifetime), like the other Steam backends' singletons.
KeyboardWatch &watch() {
  static KeyboardWatch *w = new KeyboardWatch();
  return *w;
}

}  // namespace

bool steam_floating_keyboard_dismissed() { return watch().take_dismissed(); }

#else

bool steam_floating_keyboard_dismissed() { return false; }

#endif
