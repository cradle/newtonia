#pragma once

// Unified OpenGL/GLES2 compatibility header.
// Include this instead of platform-specific GLUT/OpenGL headers.

#ifdef __ANDROID__
#include "gles2_compat.h"
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

#endif // __ANDROID__
