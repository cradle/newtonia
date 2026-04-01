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

#if defined(__ANDROID__) || defined(__IOS__) || defined(__EMSCRIPTEN__)
#include "gles2_compat.h"
#  ifdef __EMSCRIPTEN__
#    include <emscripten.h>
#    define glutLeaveMainLoop() emscripten_cancel_main_loop()
#  else
#    define glutLeaveMainLoop() // no-op on Android / iOS
#  endif
#else

// Desktop: include GLUT first (provides window management + GL types),
// then gles2_compat.h which adds the compat_ shim macros on top.
#ifdef __APPLE__
#define glutLeaveMainLoop() exit(0)
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
#endif

// Shim: redirect legacy GL calls to our VBO/VAO/shader implementation.
// Must come after GLUT so GL types are already defined.
#include "gles2_compat.h"

#endif // __ANDROID__ || __IOS__ || __EMSCRIPTEN__

// Returns true when running in Steam Game Mode (Steam Deck).
inline bool is_steam_gamemode() {
#if defined(__ANDROID__) || defined(__IOS__) || defined(__EMSCRIPTEN__) || defined(__APPLE__)
  return false;
#else
  return getenv("SteamDeck") != nullptr;
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
