// See render_target.h. Desktop builds only: the guard matches warp_pass.cpp's
// desktop GL loader, and the root-glob builds of every other platform (iOS,
// Android, the Xbox ports) compile this to an empty translation unit.
#if !defined(__ANDROID__) && !defined(__IOS__) && !defined(__EMSCRIPTEN__) && \
    !defined(_GAMING_XBOX) && !defined(_GAMING_DESKTOP)

#if defined(__linux__)
#  ifndef GL_GLEXT_PROTOTYPES
#    define GL_GLEXT_PROTOTYPES
#  endif
#endif

#include "render_target.h"
#include "gl_compat.h"
#include <SDL.h>

#if defined(__linux__)
#  include <GL/glext.h>
#elif defined(_WIN32)
#  include <GL/glext.h>
// opengl32.dll exports GL 1.1 only: load the framebuffer API at first use,
// as warp_pass.cpp does for its shaders.
#  define RT_GL_FNS \
     X(PFNGLGENFRAMEBUFFERSPROC,         glGenFramebuffers        ) \
     X(PFNGLDELETEFRAMEBUFFERSPROC,      glDeleteFramebuffers     ) \
     X(PFNGLBINDFRAMEBUFFERPROC,         glBindFramebuffer        ) \
     X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer) \
     X(PFNGLCHECKFRAMEBUFFERSTATUSPROC,  glCheckFramebufferStatus ) \
     X(PFNGLGENRENDERBUFFERSPROC,        glGenRenderbuffers       ) \
     X(PFNGLDELETERENDERBUFFERSPROC,     glDeleteRenderbuffers    ) \
     X(PFNGLBINDRENDERBUFFERPROC,        glBindRenderbuffer       ) \
     X(PFNGLRENDERBUFFERSTORAGEPROC,     glRenderbufferStorage    ) \
     X(PFNGLBLITFRAMEBUFFERPROC,         glBlitFramebuffer        )
#  define X(T, name) static T rt_##name = NULL;
   RT_GL_FNS
#  undef X
static bool rt_load_gl_fns() {
#  define X(T, name) rt_##name = (T)wglGetProcAddress(#name);
  RT_GL_FNS
#  undef X
  return rt_glGenFramebuffers && rt_glBlitFramebuffer && rt_glRenderbufferStorage;
}
#  define glGenFramebuffers          rt_glGenFramebuffers
#  define glDeleteFramebuffers       rt_glDeleteFramebuffers
#  define glBindFramebuffer          rt_glBindFramebuffer
#  define glFramebufferRenderbuffer  rt_glFramebufferRenderbuffer
#  define glCheckFramebufferStatus   rt_glCheckFramebufferStatus
#  define glGenRenderbuffers         rt_glGenRenderbuffers
#  define glDeleteRenderbuffers      rt_glDeleteRenderbuffers
#  define glBindRenderbuffer         rt_glBindRenderbuffer
#  define glRenderbufferStorage      rt_glRenderbufferStorage
#  define glBlitFramebuffer          rt_glBlitFramebuffer
#endif

static GLuint s_fbo = 0, s_colour = 0;
static int s_w = 0, s_h = 0;

bool RenderTarget::active() { return s_fbo != 0; }

void RenderTarget::release() {
  if (s_fbo) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &s_fbo);
    glDeleteRenderbuffers(1, &s_colour);
  }
  s_fbo = s_colour = 0;
  s_w = s_h = 0;
}

bool RenderTarget::ensure(int w, int h) {
  if (s_fbo && w == s_w && h == s_h) return true;
  release();
  if (w <= 0 || h <= 0) return false;
#if defined(_WIN32)
  static const bool loaded = rt_load_gl_fns();
  if (!loaded) {
    SDL_Log("resolution: no framebuffer objects in this GL; drawing at the window size");
    return false;
  }
#endif
  GLint max_rb = 0;
  glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &max_rb);
  if (max_rb > 0 && (w > max_rb || h > max_rb)) {
    SDL_Log("resolution: %dx%d is over this GPU's %d limit", w, h, (int)max_rb);
    return false;
  }
  glGenRenderbuffers(1, &s_colour);
  glBindRenderbuffer(GL_RENDERBUFFER, s_colour);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
  glBindRenderbuffer(GL_RENDERBUFFER, 0);
  glGenFramebuffers(1, &s_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, s_fbo);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_RENDERBUFFER, s_colour);
  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (status != GL_FRAMEBUFFER_COMPLETE || glGetError() != GL_NO_ERROR) {
    SDL_Log("resolution: could not make a %dx%d buffer (status 0x%x)", w, h,
            (unsigned)status);
    release();
    return false;
  }
  s_w = w;
  s_h = h;
  SDL_Log("resolution: drawing at %dx%d", w, h);
  return true;
}

void RenderTarget::bind() {
  if (s_fbo) glBindFramebuffer(GL_FRAMEBUFFER, s_fbo);
}

void RenderTarget::present(int window_w, int window_h) {
  if (!s_fbo) return;
  glBindFramebuffer(GL_READ_FRAMEBUFFER, s_fbo);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glBlitFramebuffer(0, 0, s_w, s_h, 0, 0, window_w, window_h,
                    GL_COLOR_BUFFER_BIT, GL_LINEAR);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

#endif // desktop
