#pragma once

// OpenGL ES 2.0 compatibility shim.
// Provides OpenGL 1.x immediate-mode / fixed-function API on top of GLES2.

#if defined(__ANDROID__)
#include <GLES2/gl2.h>
#elif defined(__IOS__)
#include <OpenGLES/ES2/gl.h>
#elif defined(__EMSCRIPTEN__)
// SDL2's bundled GLES2 header — always available under USE_SDL=2.
// Avoids depending on the Emscripten sysroot cache being pre-warmed.
#include <SDL_opengles2.h>
#endif
#include <SDL.h>

// GLdouble / GLclampd are desktop-only; define them for the shim API.
typedef double  GLdouble;
typedef double  GLclampd;

// ---- Constants not present in GLES2 ----
#define GL_POLYGON              0x0009
#define GL_QUADS                0x0007

// Fixed-function matrix modes (not in GLES2, but we emulate them)
#ifndef GL_MODELVIEW
#define GL_MODELVIEW            0x1700
#endif
#ifndef GL_PROJECTION
#define GL_PROJECTION           0x1701
#endif

// Smoothing hints (not in GLES2 – handled as no-ops)
#ifndef GL_LINE_SMOOTH
#define GL_LINE_SMOOTH          0x0B20
#endif
#ifndef GL_POINT_SMOOTH
#define GL_POINT_SMOOTH         0x0B10
#endif
#ifndef GL_LINE_SMOOTH_HINT
#define GL_LINE_SMOOTH_HINT     0x0C52
#endif
#ifndef GL_POINT_SMOOTH_HINT
#define GL_POINT_SMOOTH_HINT    0x0C51
#endif
#ifndef GL_NICEST
#define GL_NICEST               0x1102
#endif

// Accumulation buffer (not in GLES2 – all no-ops)
#ifndef GL_ACCUM_BUFFER_BIT
#define GL_ACCUM_BUFFER_BIT     0x00000200
#define GL_ACCUM                0x0100
#define GL_MULT                 0x0103
#define GL_RETURN               0x0102
#endif

// Display list mode
#define GL_COMPILE              0x1300

// GLUT stubs used by this codebase
#define GLUT_ELAPSED_TIME       700
#define GLUT_VISIBLE            1
#define GLUT_KEY_F1             1
#define GLUT_KEY_F4             4
#define GLUT_KEY_F8             8
#define GLUT_KEY_F11            11
#define GLUT_ACTIVE_ALT         4
#define GLUT_WINDOW_WIDTH       100
#define GLUT_WINDOW_HEIGHT      101

// ---- Initialisation / shutdown ----
void gles2_init();
void gles2_shutdown();

// ---- Matrix stack ----
void glMatrixMode(GLenum mode);
void glLoadIdentity();
void glPushMatrix();
void glPopMatrix();
void glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z);
void glScalef(GLfloat x, GLfloat y, GLfloat z);
void glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
             GLdouble zNear, GLdouble zFar);

// GLU helpers (declared here, not in a separate glu.h on Android)
void gluOrtho2D(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top);
void gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar);
void gluLookAt(GLdouble eyeX,    GLdouble eyeY,    GLdouble eyeZ,
               GLdouble centerX, GLdouble centerY, GLdouble centerZ,
               GLdouble upX,     GLdouble upY,     GLdouble upZ);

// ---- Immediate-mode drawing ----
void glBegin(GLenum mode);
void glEnd();

void glVertex2f(GLfloat x, GLfloat y);
void glVertex2i(GLint   x, GLint   y);
void glVertex2fv(const GLfloat *v);
void glVertex3f(GLfloat x, GLfloat y, GLfloat z);

void glColor3f(GLfloat r, GLfloat g, GLfloat b);
void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void glColor3fv(const GLfloat *v);
void glColor4fv(const GLfloat *v);

// ---- Point / line size ----
// glLineWidth exists in GLES2 but with limited support; keep as-is.
// glPointSize does NOT exist in GLES2 – we emulate via a shader uniform.
void glPointSize(GLfloat size);

// ---- Display lists ----
GLuint glGenLists(GLsizei range);
void   glNewList(GLuint list, GLenum mode);
void   glEndList();
void   glCallList(GLuint list);
void   glDeleteLists(GLuint list, GLsizei range);

// ---- No-ops for features not in GLES2 ----
inline void glAccum(GLenum /*op*/, GLfloat /*value*/) {}
inline void glClearAccum(GLfloat /*r*/, GLfloat /*g*/,
                         GLfloat /*b*/, GLfloat /*a*/) {}

// glEnable / glDisable exist in GLES2; unsupported caps produce GL_INVALID_ENUM
// which is harmless, so we let callers use the GLES2 versions directly.

// ---- GLUT timing stub ----
int glutGet(GLenum query);
