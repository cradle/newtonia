#include "presence.h"
#include <cstdio>
#include <iostream>
#include <string>

namespace Presence {

// Platform backends implement Backend::set_menu/set_level/clear behind
// their own build flags (Steam: steam_presence.cpp). The dedupe below is
// shared and must not move into a backend.
#ifdef STEAM_BUILD
namespace Backend {  // steam_presence.cpp
  void set_menu();
  void set_hosting();
  void set_joining();
  void set_level(int level, int num_players);
  void clear();
}
#endif

namespace {

// States re-report on every constructor and level rebuild, so dedupe here:
// backends only hear real changes, and the stdout trace — the only visible
// effect on builds without a platform backend, and what headless tests can
// grep for — stays one line per change.
std::string last_status;

bool status_changed(const std::string &status) {
  if (status == last_status) return false;
  last_status = status;
  std::cout << "Presence: " << status << std::endl;
  return true;
}

} // namespace

void set_menu() {
  if (!status_changed("In the Menu")) return;
#ifdef STEAM_BUILD
  Backend::set_menu();
#endif
}

void set_hosting() {
  if (!status_changed("Hosting a Co-Op Game")) return;
#ifdef STEAM_BUILD
  Backend::set_hosting();
#endif
}

void set_joining() {
  if (!status_changed("Joining a Co-Op Game")) return;
#ifdef STEAM_BUILD
  Backend::set_joining();
#endif
}

void set_level(int level, int num_players) {
  char status[32];
  std::snprintf(status, sizeof(status), "Level %d%s", level,
                num_players >= 2 ? " Co-Op" : "");
  if (!status_changed(status)) return;
#ifdef STEAM_BUILD
  Backend::set_level(level, num_players);
#endif
}

void clear() {
  if (last_status.empty()) return;
  last_status.clear();
#ifdef STEAM_BUILD
  Backend::clear();
#endif
}

} // namespace Presence
