#pragma once

// Unified OpenGL/GLES2 compatibility header.
// Include this instead of platform-specific GLUT/OpenGL headers.
//
// On desktop (macOS/Linux/Windows):
//   1. GLUT headers are included for window-management functions.
//   2. gles2_compat.h is included to redirect all legacy GL calls
//      (glBegin/glEnd, display lists, matrix stack, glPointSize, …)
//      through our VBO + VAO + GLSL shim.
//
// On GLES2 (Android/iOS/Web):
//   gles2_compat.h provides the full fixed-function emulation layer.

#if defined(__ANDROID__) || defined(__IOS__) || defined(__EMSCRIPTEN__) || \
    defined(_GAMING_XBOX) || defined(_GAMING_DESKTOP)
#include "gles2_compat.h"
#  ifdef __EMSCRIPTEN__
#    include <emscripten.h>
#    define glutLeaveMainLoop() emscripten_cancel_main_loop()
#  elif defined(_GAMING_XBOX) || defined(_GAMING_DESKTOP)
#    define glutLeaveMainLoop() exit(0)
#  else
#    define glutLeaveMainLoop() // no-op on Android / iOS
#  endif
#else

// Desktop: include GLUT first (provides window management + GL types),
// then gles2_compat.h which adds the compat_ shim macros on top.
#ifdef __APPLE__
#define glutLeaveMainLoop() exit(0)
// gl3.h defines __gl3_h_ (not __gl_h_), so GLUT's gl.h would still be
// processed and emit a "both included" warning.  Manually define __gl_h_
// after pulling in gl3.h to block GLUT from re-including gl.h at all.
#include <OpenGL/gl3.h>
#define __gl_h_
#include <GLUT/glut.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/glut.h>
#ifndef __APPLE__
#include <GL/freeglut_std.h>
#include <GL/freeglut_ext.h>
#endif
#ifdef __linux__
// On Linux, GL 2.0+ functions are exported by libGL and can be declared as
// regular extern symbols.  GL_GLEXT_PROTOTYPES enables those declarations in
// <GL/glext.h>.  Must be defined before the first inclusion of glext.h.
// On Windows/MinGW we skip this: glext.h would declare them as regular
// functions which would conflict with our wglGetProcAddress pointer variables.
#ifndef GL_GLEXT_PROTOTYPES
#  define GL_GLEXT_PROTOTYPES
#endif
#include <GL/glext.h>
#endif // __linux__
#endif

// Shim: redirect legacy GL calls to our VBO/VAO/shader implementation.
// Must come after GLUT so GL types are already defined.
#include "gles2_compat.h"

#endif // __ANDROID__ || __IOS__ || __EMSCRIPTEN__

// Steam SDK wrapper — init/shutdown/callbacks/branch detection.
// Safe no-ops when STEAM_BUILD is not defined.
#include "steam_build.h"

// Returns the Steam beta branch name (e.g. "beta"), or empty string on the
// default/public branch or on non-Steam builds.
inline std::string get_steam_branch() {
  return steam_get_branch();
}

// Returns true when running in Steam Game Mode (Steam Deck).
inline bool is_steam_gamemode() {
#if defined(__ANDROID__) || defined(__IOS__) || defined(__EMSCRIPTEN__) || defined(__APPLE__) || \
    defined(_GAMING_XBOX) || defined(_GAMING_DESKTOP)
  return false;
#else
  return getenv("SteamDeck") != nullptr;
#endif
}

// Returns true when the macOS high-priority game activity is held.
// On macOS 14+ (Sonoma) this also means the OS has activated Game Mode.
// Always false on non-Apple platforms.
#if defined(__APPLE__) && !defined(__IOS__)
extern "C" int is_game_mode_active_macos();
#endif
inline bool is_game_mode_active() {
#if defined(__APPLE__) && !defined(__IOS__)
  return is_game_mode_active_macos() != 0;
#else
  return false;
#endif
}

// Returns true when the primary input is touch (Android, iOS, or web with a
// coarse pointer such as a touchscreen).
inline bool is_touch_mode() {
#if defined(__ANDROID__) || defined(__IOS__)
  return true;
#elif defined(__EMSCRIPTEN__)
  return EM_ASM_INT(return window.matchMedia('(pointer: coarse)').matches ? 1 : 0;) != 0;
#else
  return false;
#endif
}
