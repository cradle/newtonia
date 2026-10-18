#ifndef HAPTICS_H
#define HAPTICS_H

// Haptics: a short buzz when something happens to YOUR ship — the phone
// itself on touch, the controller's rumble motors on a pad. One seam, two
// backends:
//
//   - Pad rumble (every build): SDL_GameControllerRumble on the pad the
//     seat is flying with. SDL drives the platform's own path (XInput/GDK
//     on Windows and Xbox, the InputDevice vibrators on Android, HID on
//     the rest). A Steam Input pad the backend ADOPTED has no SDL device
//     to rumble and stays still; one Steam holds on a gamepad template is
//     an SDL pad and rumbles.
//   - Phone vibration: Android (NewtoniaActivity.vibrate over JNI, skipped
//     on a car display — the phone is in a cradle) and the web build
//     (navigator.vibrate — Android browsers; iOS Safari has none). Other
//     builds have no device motor and do nothing.
//
// GLGame decides WHICH ship an event belongs to and whether that ship is
// flown from this machine (GLGame::update_haptics); this file only knows
// how strong each event feels. Preferences::vibration gates everything.

#include "pad.h"

namespace Haptics {

// Strongest last: a weaker buzz never cuts a stronger one short.
enum Event {
  PICKUP = 0,     // collected an item
  BOOST,          // boost burst fired
  LEVEL_CLEAR,    // the field is clear, countdown starting
  NOVA,           // nova detonated
  DEATH,          // ship destroyed
  EVENT_COUNT
};

// Rumble one pad (no-op for PAD_NONE, a Steam-adopted pad, or a pad
// without motors).
void pad(PadId id, Event e);

// Vibrate the device itself (no-op where there is no motor).
void phone(Event e);

// Android: the activity moved to (or off) a car's projected display —
// the phone is in a cradle there, so it stays still.
void set_external_display(bool external);

}  // namespace Haptics

#endif
