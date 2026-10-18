#ifndef LAUNCH_SHORTCUT_H
#define LAUNCH_SHORTCUT_H

// Launcher shortcuts: the actions a long press on the app icon offers
// (Android's static shortcuts, android/app/src/main/res/xml/shortcuts.xml).
// The platform entry point notes which one launched the game, from any
// thread; Menu::tick takes it and acts on it like the matching menu row.
// Nothing else notes one, so other builds only see the
// NEWTONIA_LAUNCH_SHORTCUT test hook (see take()).

namespace LaunchShortcut {

enum Kind {
  NONE = 0,
  CONTINUE,   // the solo save (falls back to the menu without one)
  NEW_GAME,   // a fresh game (asks first when it would overwrite a save)
  ONLINE,     // the online lobby
};

// Record a shortcut by its id ("continue", "new_game", "online"); an
// unknown id is ignored. Thread-safe.
void note(const char *id);

// The pending shortcut, cleared by the read. One noted more than a few
// seconds ago comes back NONE: a shortcut tapped while a game was running
// is about that moment, not whenever the player next reaches the menu.
Kind take();

}  // namespace LaunchShortcut

#endif
