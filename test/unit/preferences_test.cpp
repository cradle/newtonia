// New-install detection for the touch layout defaults (preferences.cpp
// load_preferences): a fresh pref path starts on ONE HAND + RIGHT and
// has that written on the spot; anything that predates the change — an
// INI without the keys, or no INI beside any other game file — keeps
// the classic TWO HANDS + CENTRE. Linux only (SDL's pref path honours
// XDG_DATA_HOME, so every case gets its own empty directory). Run via
// test/unit/preferences.sh.
#include "preferences.h"
#include "audio_volume.h"
#include <SDL.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>

void web_fs_sync(const char *) {}
namespace AudioVolume { void apply() {} }

static std::string contents(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f),
                     std::istreambuf_iterator<char>());
}

static bool exists(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0;
}

// A fresh XDG_DATA_HOME per case; returns the game's pref directory in it.
static std::string fresh_pref_dir() {
  char root[] = "/tmp/newtonia-prefs-test.XXXXXX";
  assert(mkdtemp(root));
  assert(setenv("XDG_DATA_HOME", root, 1) == 0);
  char *pref = SDL_GetPrefPath("cc.gfm", "newtonia");
  assert(pref);
  std::string dir(pref);
  SDL_free(pref);
  return dir;
}

static void touch(const std::string &path, const char *text = "") {
  FILE *f = fopen(path.c_str(), "wb");
  assert(f);
  fputs(text, f);
  fclose(f);
}

int main() {
  // 1. Nothing in the pref path: a new install. ONE HAND + RIGHT, and the
  //    INI is written at once so the decision sticks.
  {
    std::string dir = fresh_pref_dir();
    load_preferences();
    assert(preferences_first_launch());
    assert(g_prefs.touch_one_hand);
    assert(g_prefs.touch_handedness == 2);
    assert(!g_prefs.touch_help_done);  // the first game still shows the card
    std::string ini = contents(dir + "preferences.ini");
    assert(ini.find("touch_one_hand=1\n") != std::string::npos);
    assert(ini.find("touch_handedness=2\n") != std::string::npos);
    // The install now looks old on every later launch, whatever play
    // leaves beside the INI — and reads back as the layout it chose.
    touch(dir + "stats.dat");
    g_prefs.touch_one_hand = false;
    load_preferences();
    assert(!preferences_first_launch());
    assert(g_prefs.touch_one_hand);
    assert(g_prefs.touch_handedness == 2);
  }

  // 2. No INI, but play left a file behind (a mobile install that never
  //    changed a setting): an old install — classic layout, no write.
  {
    static const char *const files[] = {
        "savegame.dat", "online_savegame.dat", "stats.dat", "highscore.dat",
        "pending_achievements.dat", "netplay_resume.dat"};
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
      std::string dir = fresh_pref_dir();
      touch(dir + files[i]);
      load_preferences();
      assert(!preferences_first_launch());
      assert(!g_prefs.touch_one_hand);
      assert(g_prefs.touch_handedness == 1);
      assert(!exists(dir + "preferences.ini"));
    }
    // The replays DIRECTORY counts too.
    std::string dir = fresh_pref_dir();
    assert(mkdir((dir + "replays").c_str(), 0700) == 0);
    load_preferences();
    assert(!preferences_first_launch());
    assert(!g_prefs.touch_one_hand);
    assert(!exists(dir + "preferences.ini"));
  }

  // 3. An INI from a build that predates the keys: the layout that INI was
  //    written under, i.e. the classic one; the file is left alone.
  {
    std::string dir = fresh_pref_dir();
    touch(dir + "preferences.ini", "fullscreen=0\nstar_density=0.50\n");
    load_preferences();
    assert(!preferences_first_launch());
    assert(!g_prefs.touch_one_hand);
    assert(g_prefs.touch_handedness == 1);
    assert(!g_prefs.fullscreen);
    assert(contents(dir + "preferences.ini") == "fullscreen=0\nstar_density=0.50\n");
  }

  // 4. Explicit values win, whatever they are.
  {
    std::string dir = fresh_pref_dir();
    touch(dir + "preferences.ini", "touch_one_hand=0\ntouch_handedness=0\n");
    load_preferences();
    assert(!g_prefs.touch_one_hand);
    assert(g_prefs.touch_handedness == 0);
    touch(dir + "preferences.ini", "touch_one_hand=1\ntouch_handedness=1\n");
    load_preferences();
    assert(g_prefs.touch_one_hand);
    assert(g_prefs.touch_handedness == 1);
  }

  // 5. A peek on an empty pref path (the shot/video harnesses, the signal
  //    self-test): struct defaults, no decision, nothing written.
  {
    std::string dir = fresh_pref_dir();
    load_preferences(false);
    assert(!preferences_first_launch());
    assert(!g_prefs.touch_one_hand);
    assert(g_prefs.touch_handedness == 1);
    assert(!exists(dir + "preferences.ini"));
    // ...and a peek still reads an INI that is there.
    touch(dir + "preferences.ini", "touch_one_hand=1\ntouch_handedness=2\n");
    load_preferences(false);
    assert(g_prefs.touch_one_hand);
    assert(g_prefs.touch_handedness == 2);
  }

  std::printf("preferences_test: all checks passed\n");
  return 0;
}
