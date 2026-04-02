#pragma once

// OpenGL ES 2.0 / Desktop OpenGL compatibility shim.
// On GLES2 (Android/iOS/Web): provides a subset of the OpenGL 1.x fixed-
//   function API on top of GLES2 shaders + vertex arrays.
// On Desktop (macOS/Linux/Windows): the same shim routes all legacy
//   fixed-function calls through modern GL 3.3 Core shaders + VBOs + VAO,
//   eliminating display-list and immediate-mode usage everywhere.
//
// INCLUDE ORDER: on desktop this header must be included AFTER the GLUT/GL
// headers (gl_compat.h guarantees this).  gles2_compat.cpp includes its own
// GL headers before including this file.

#if defined(__ANDROID__) || defined(__IOS__) || defined(__EMSCRIPTEN__)
// ---- GLES2 platform GL headers ----
#  if defined(__ANDROID__)
#    include <GLES2/gl2.h>
#  elif defined(__IOS__)
#    include <OpenGLES/ES2/gl.h>
#  elif defined(__EMSCRIPTEN__)
#    include <SDL_opengles2.h>
#  endif
#else
// ---- Desktop: signal that we need the compat_ aliasing scheme ----
// GL types (GLuint, GLenum, …) are already available from the GLUT/GL
// headers included before this file (see gl_compat.h).
#  define DESKTOP_COMPAT_GL 1
#endif

#include <SDL.h>

// GLdouble / GLclampd: needed for the shim API on GLES2 (not in gl2.h).
// On desktop the GL/GLUT headers already provide them.
#ifndef DESKTOP_COMPAT_GL
typedef double  GLdouble;
typedef double  GLclampd;
#endif

// ---- Constants not present in GLES2 ----
#ifndef GL_POLYGON
#  define GL_POLYGON              0x0009
#endif
#ifndef GL_QUADS
#  define GL_QUADS                0x0007
#endif

// Fixed-function matrix modes
#ifndef GL_MODELVIEW
#  define GL_MODELVIEW            0x1700
#endif
#ifndef GL_PROJECTION
#  define GL_PROJECTION           0x1701
#endif

// Smoothing hints (no-ops in our shim)
#ifndef GL_LINE_SMOOTH
#  define GL_LINE_SMOOTH          0x0B20
#endif
#ifndef GL_POINT_SMOOTH
#  define GL_POINT_SMOOTH         0x0B10
#endif
#ifndef GL_LINE_SMOOTH_HINT
#  define GL_LINE_SMOOTH_HINT     0x0C52
#endif
#ifndef GL_POINT_SMOOTH_HINT
#  define GL_POINT_SMOOTH_HINT    0x0C51
#endif
#ifndef GL_NICEST
#  define GL_NICEST               0x1102
#endif

// Accumulation buffer (no-ops in our shim)
#ifndef GL_ACCUM_BUFFER_BIT
#  define GL_ACCUM_BUFFER_BIT     0x00000200
#  define GL_ACCUM                0x0100
#  define GL_MULT                 0x0103
#  define GL_RETURN               0x0102
#endif

// Display list compile mode
#ifndef GL_COMPILE
#  define GL_COMPILE              0x1300
#endif

// GLUT stubs (used by GLES2 / web platforms only; desktop has the real GLUT).
#ifndef DESKTOP_COMPAT_GL
#define GLUT_ELAPSED_TIME         700
#define GLUT_VISIBLE              1
#define GLUT_KEY_F1               1
#define GLUT_KEY_F4               4
#define GLUT_KEY_F8               8
#define GLUT_KEY_F11              11
#define GLUT_ACTIVE_ALT           4
#define GLUT_WINDOW_WIDTH         100
#define GLUT_WINDOW_HEIGHT        101
#endif

// ============================================================
// Shim API — init / shutdown / MVP access
// ============================================================

void gles2_init();
void gles2_shutdown();

// Returns the combined Projection × ModelView matrix (column-major, 16 floats).
// Used by WarpPass on all platforms.
void gles2_get_mvp(float mvp[16]);

// Per-draw tint multiplied against vertex colour in the fragment shader.
// Call gles2_set_tint(r,g,b,a) before drawing, reset to (1,1,1,1) afterwards.
void gles2_set_tint(float r, float g, float b, float a);

// Exposes internal shader/VAO handles so that Mesh and other GPU objects
// can share the same program without duplicating shader state.
struct GLCompatProg {
    GLuint prog;
    GLint  attr_pos, attr_col;
    GLint  uni_mvp, uni_ptsz, uni_ispt, uni_tint;
#ifdef DESKTOP_COMPAT_GL
    GLuint vao;
#endif
};
const GLCompatProg* gles2_program_info();

// ============================================================
// Point / line size (intercepted on all platforms)
// ============================================================

// glPointSize does not exist in GLES2 or GL 3.3 Core; emulated via shader uniform.
// glLineWidth is clamped to 1 on macOS/Metal/WebGL; emulated with screen-space quads.
void gles2_set_line_width(GLfloat width);
void gles2_set_viewport(GLint x, GLint y, GLsizei w, GLsizei h);

// These macros intercept all call sites on every platform.
#define glLineWidth(w)           gles2_set_line_width(w)
#define glViewport(x,y,w,h)     gles2_set_viewport((x),(y),(w),(h))

// ============================================================
// On GLES2: functions use their standard OpenGL names directly
// (GLES2 does not export them, so there is no symbol conflict).
//
// On Desktop: functions are given a compat_ prefix to avoid
// duplicate-symbol conflicts with libGL/OpenGL framework, which
// still exports all legacy symbols even in a Core Profile context.
// Macros redirect every call site to the compat_ version.
// ============================================================

#ifdef DESKTOP_COMPAT_GL

// ---- Matrix stack ----
void compat_glMatrixMode(GLenum mode);
void compat_glLoadIdentity();
void compat_glPushMatrix();
void compat_glPopMatrix();
void compat_glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void compat_glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void compat_glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z);
void compat_glScalef(GLfloat x, GLfloat y, GLfloat z);
void compat_glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
                    GLdouble zNear, GLdouble zFar);

// ---- GLU helpers ----
void compat_gluOrtho2D(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top);
void compat_gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar);
void compat_gluLookAt(GLdouble eyeX,    GLdouble eyeY,    GLdouble eyeZ,
                      GLdouble centerX, GLdouble centerY, GLdouble centerZ,
                      GLdouble upX,     GLdouble upY,     GLdouble upZ);

// ---- Immediate-mode drawing ----
void compat_glBegin(GLenum mode);
void compat_glEnd();
void compat_glVertex2f(GLfloat x, GLfloat y);
void compat_glVertex2i(GLint   x, GLint   y);
void compat_glVertex2fv(const GLfloat *v);
void compat_glVertex3f(GLfloat x, GLfloat y, GLfloat z);
void compat_glColor3f(GLfloat r, GLfloat g, GLfloat b);
void compat_glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void compat_glColor3fv(const GLfloat *v);
void compat_glColor4fv(const GLfloat *v);

// ---- Point size ----
void compat_glPointSize(GLfloat size);

// ---- Display lists ----
GLuint compat_glGenLists(GLsizei range);
void   compat_glNewList(GLuint list, GLenum mode);
void   compat_glEndList();
void   compat_glCallList(GLuint list);
void   compat_glDeleteLists(GLuint list, GLsizei range);

// ---- No-ops (accumulation buffer removed from Core Profile) ----
inline void compat_glAccum(GLenum, GLfloat) {}
inline void compat_glClearAccum(GLfloat, GLfloat, GLfloat, GLfloat) {}

// ---- Redirect macros ----
#define glMatrixMode(m)              compat_glMatrixMode(m)
#define glLoadIdentity()             compat_glLoadIdentity()
#define glPushMatrix()               compat_glPushMatrix()
#define glPopMatrix()                compat_glPopMatrix()
#define glTranslatef(x,y,z)          compat_glTranslatef((x),(y),(z))
#define glRotatef(a,x,y,z)           compat_glRotatef((a),(x),(y),(z))
#define glRotated(a,x,y,z)           compat_glRotated((a),(x),(y),(z))
#define glScalef(x,y,z)              compat_glScalef((x),(y),(z))
#define glOrtho(l,r,b,t,n,f)         compat_glOrtho((l),(r),(b),(t),(n),(f))
#define gluOrtho2D(l,r,b,t)          compat_gluOrtho2D((l),(r),(b),(t))
#define gluPerspective(fv,a,n,f)     compat_gluPerspective((fv),(a),(n),(f))
#define gluLookAt(ex,ey,ez,cx,cy,cz,ux,uy,uz) \
    compat_gluLookAt((ex),(ey),(ez),(cx),(cy),(cz),(ux),(uy),(uz))
#define glBegin(m)                   compat_glBegin(m)
#define glEnd()                      compat_glEnd()
#define glVertex2f(x,y)              compat_glVertex2f((x),(y))
#define glVertex2i(x,y)              compat_glVertex2i((x),(y))
#define glVertex2fv(v)               compat_glVertex2fv(v)
#define glVertex3f(x,y,z)            compat_glVertex3f((x),(y),(z))
#define glColor3f(r,g,b)             compat_glColor3f((r),(g),(b))
#define glColor4f(r,g,b,a)           compat_glColor4f((r),(g),(b),(a))
#define glColor3fv(v)                compat_glColor3fv(v)
#define glColor4fv(v)                compat_glColor4fv(v)
#define glPointSize(s)               compat_glPointSize(s)
#define glGenLists(n)                compat_glGenLists(n)
#define glNewList(id,m)              compat_glNewList((id),(m))
#define glEndList()                  compat_glEndList()
#define glCallList(id)               compat_glCallList(id)
#define glDeleteLists(id,n)          compat_glDeleteLists((id),(n))
#define glAccum(op,val)              compat_glAccum((op),(val))
#define glClearAccum(r,g,b,a)        compat_glClearAccum((r),(g),(b),(a))

#else // GLES2

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

// ---- GLU helpers ----
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

// ---- Point size ----
void glPointSize(GLfloat size);

// ---- Display lists ----
GLuint glGenLists(GLsizei range);
void   glNewList(GLuint list, GLenum mode);
void   glEndList();
void   glCallList(GLuint list);
void   glDeleteLists(GLuint list, GLsizei range);

// ---- No-ops ----
inline void glAccum(GLenum, GLfloat) {}
inline void glClearAccum(GLfloat, GLfloat, GLfloat, GLfloat) {}

// ---- GLUT timing stub (desktop has the real glutGet) ----
int glutGet(GLenum query);

#endif // DESKTOP_COMPAT_GL
