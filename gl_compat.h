#pragma once

// Unified OpenGL/GLES2 compatibility header.
// Include this instead of platform-specific GLUT/OpenGL headers.

#if defined(__ANDROID__) || defined(__IOS__) || defined(__EMSCRIPTEN__)
#include "gles2_compat.h"
#  ifdef __EMSCRIPTEN__
#    include <emscripten.h>
#    define glutLeaveMainLoop() emscripten_cancel_main_loop()
#  else
#    define glutLeaveMainLoop() // no-op on Android / iOS
#  endif
#else

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

#endif // __ANDROID__ || __IOS__ || __EMSCRIPTEN__
