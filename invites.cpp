#include "invites.h"

#include <cctype>
#include <cstring>
#include <mutex>

// Shared invite layer: owns the pending-code handoff and the connect-string
// parsing, so every backend (and the cold-launch path) funnels through one
// place. Platform backends implement Backend::init/set_joinable/clear_joinable
// behind their own build flags; there is no default backend, so builds without
// a platform integration carry zero cost — capture_launch still works, so a
// "+connect <code>" on the command line joins even without one.

#ifdef STEAM_BUILD
namespace Invites {
namespace Backend {  // steam_invites.cpp
void init();
void set_joinable(const std::string &room_code);
void clear_joinable();
}
}
#endif

namespace Invites {

namespace {

// A room code accepted (via callback or launch arg) and awaiting the menu's
// poll. Empty when nothing is pending. Guarded by s_mutex: the deep-link
// backends deliver on a platform thread (Android's UI thread, an iOS UIKit
// callback) while Menu::tick drains on the game thread.
std::string s_pending;
std::mutex s_mutex;

// One-shot "raise the window" request, set alongside s_pending when an invite
// is accepted and drained by the desktop entry point (glut.cpp). Same guard.
bool s_focus_request = false;

// The advertised connect string is "+connect <code>"; pull <code> back out.
// Tolerant of a bare code (no "+connect" prefix) so capture_launch can hand
// us either form.
std::string parse_connect(const char *s) {
  if (!s) return std::string();
  std::string in(s);
  const std::string key = "+connect";
  size_t p = in.find(key);
  size_t start = (p == std::string::npos) ? 0 : p + key.size();
  while (start < in.size() && std::isspace((unsigned char)in[start])) start++;
  size_t end = start;
  while (end < in.size() && !std::isspace((unsigned char)in[end])) end++;
  return in.substr(start, end - start);
}

} // namespace

void init() {
#ifdef STEAM_BUILD
  Backend::init();
#endif
}

void set_joinable(const std::string &room_code) {
#ifdef STEAM_BUILD
  Backend::set_joinable(room_code);
#else
  (void)room_code;
#endif
}

void clear_joinable() {
#ifdef STEAM_BUILD
  Backend::clear_joinable();
#endif
}

void note_accepted(const char *connect_string) {
  std::string code = parse_connect(connect_string);
  if (code.empty()) return;
  std::lock_guard<std::mutex> lock(s_mutex);
  s_pending = code;
  s_focus_request = true;  // raise the window if we're already running
}

bool poll_accepted_invite(std::string &code_out) {
  std::lock_guard<std::mutex> lock(s_mutex);
  if (s_pending.empty()) return false;
  code_out = s_pending;
  s_pending.clear();
  return true;
}

bool take_focus_request() {
  std::lock_guard<std::mutex> lock(s_mutex);
  if (!s_focus_request) return false;
  s_focus_request = false;
  return true;
}

void capture_launch(int argc, char **argv) {
  // The platform appends the connect string to the command line on a cold
  // launch. Look for the "+connect <code>" pair we advertise.
  for (int i = 0; i + 1 < argc; i++) {
    if (argv[i] && std::strcmp(argv[i], "+connect") == 0) {
      note_accepted(argv[i + 1]);  // parse_connect tolerates the bare code
      return;
    }
  }
}

} // namespace Invites
