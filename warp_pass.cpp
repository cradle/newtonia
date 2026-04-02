// ============================================================
// Platform GL setup
// ============================================================
//
// Desktop targets need OpenGL 2.0+ shader API declarations/loaders
// that are absent from the vanilla GL 1.1 headers shipped with GLUT.
//
//   Linux   – GL_GLEXT_PROTOTYPES causes <GL/glext.h> to declare
//             all the function prototypes; libGL exports them.
//   Windows – opengl32.dll only exports GL 1.1; we load 2.0+
//             functions at runtime via wglGetProcAddress.
//   macOS   – <OpenGL/gl3.h> (pulled in by gl_compat.h on desktop)
//             already exposes the full Core Profile API.
//   GLES2   – shader API is core; gles2_compat.h provides the
//             necessary MVP accessor.

#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#  ifndef GL_GLEXT_PROTOTYPES
#    define GL_GLEXT_PROTOTYPES
#  endif
#endif

#include "warp_pass.h"   // pulls in gl_compat.h → platform GL/GLUT headers
                         // On macOS, gl_compat.h now includes gl3.h before GLUT.

// Post-GL-header includes and Windows function loader.
#if !defined(__ANDROID__) && !defined(__IOS__) && !defined(__EMSCRIPTEN__)
#  if defined(__linux__)
#    include <GL/glext.h>

#  elif defined(_WIN32)
#    include <GL/glext.h>

#    define WARP_GL_FNS \
       X(PFNGLCREATESHADERPROC,             glCreateShader           ) \
       X(PFNGLSHADERSOURCEPROC,             glShaderSource           ) \
       X(PFNGLCOMPILESHADERPROC,            glCompileShader          ) \
       X(PFNGLGETSHADERIVPROC,              glGetShaderiv            ) \
       X(PFNGLGETSHADERINFOLOGPROC,         glGetShaderInfoLog       ) \
       X(PFNGLCREATEPROGRAMPROC,            glCreateProgram          ) \
       X(PFNGLATTACHSHADERPROC,             glAttachShader           ) \
       X(PFNGLLINKPROGRAMPROC,              glLinkProgram            ) \
       X(PFNGLDELETESHADERPROC,             glDeleteShader           ) \
       X(PFNGLDELETEPROGRAMPROC,            glDeleteProgram          ) \
       X(PFNGLGETPROGRAMIVPROC,             glGetProgramiv           ) \
       X(PFNGLGETPROGRAMINFOLOGPROC,        glGetProgramInfoLog      ) \
       X(PFNGLGETATTRIBLOCATIONPROC,        glGetAttribLocation      ) \
       X(PFNGLGETUNIFORMLOCATIONPROC,       glGetUniformLocation     ) \
       X(PFNGLUSEPROGRAMPROC,               glUseProgram             ) \
       X(PFNGLACTIVETEXTUREPROC,            glActiveTexture          ) \
       X(PFNGLUNIFORM1IPROC,                glUniform1i              ) \
       X(PFNGLUNIFORMMATRIX4FVPROC,         glUniformMatrix4fv       ) \
       X(PFNGLUNIFORM2FPROC,                glUniform2f              ) \
       X(PFNGLUNIFORM1FPROC,                glUniform1f              ) \
       X(PFNGLENABLEVERTEXATTRIBARRAYPROC,  glEnableVertexAttribArray) \
       X(PFNGLVERTEXATTRIBPOINTERPROC,      glVertexAttribPointer    ) \
       X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray)\
       X(PFNGLGENBUFFERSPROC,               glGenBuffers             ) \
       X(PFNGLBINDBUFFERPROC,               glBindBuffer             ) \
       X(PFNGLBUFFERDATAPROC,               glBufferData             ) \
       X(PFNGLDELETEBUFFERSPROC,            glDeleteBuffers          ) \
       X(PFNGLGENVERTEXARRAYSPROC,          glGenVertexArrays        ) \
       X(PFNGLBINDVERTEXARRAYPROC,          glBindVertexArray        ) \
       X(PFNGLDELETEVERTEXARRAYSPROC,       glDeleteVertexArrays     )

#    define X(T, name) static T wp_##name = NULL;
     WARP_GL_FNS
#    undef X

     static void warp_load_gl_fns() {
#      define X(T, name) wp_##name = (T)wglGetProcAddress(#name);
       WARP_GL_FNS
#      undef X
     }

#    define glCreateShader            wp_glCreateShader
#    define glShaderSource            wp_glShaderSource
#    define glCompileShader           wp_glCompileShader
#    define glGetShaderiv             wp_glGetShaderiv
#    define glGetShaderInfoLog        wp_glGetShaderInfoLog
#    define glCreateProgram           wp_glCreateProgram
#    define glAttachShader            wp_glAttachShader
#    define glLinkProgram             wp_glLinkProgram
#    define glDeleteShader            wp_glDeleteShader
#    define glDeleteProgram           wp_glDeleteProgram
#    define glGetProgramiv            wp_glGetProgramiv
#    define glGetProgramInfoLog       wp_glGetProgramInfoLog
#    define glGetAttribLocation       wp_glGetAttribLocation
#    define glGetUniformLocation      wp_glGetUniformLocation
#    define glUseProgram              wp_glUseProgram
#    define glActiveTexture           wp_glActiveTexture
#    define glUniform1i               wp_glUniform1i
#    define glUniformMatrix4fv        wp_glUniformMatrix4fv
#    define glUniform2f               wp_glUniform2f
#    define glUniform1f               wp_glUniform1f
#    define glEnableVertexAttribArray  wp_glEnableVertexAttribArray
#    define glVertexAttribPointer      wp_glVertexAttribPointer
#    define glDisableVertexAttribArray wp_glDisableVertexAttribArray
#    define glGenBuffers               wp_glGenBuffers
#    define glBindBuffer               wp_glBindBuffer
#    define glBufferData               wp_glBufferData
#    define glDeleteBuffers            wp_glDeleteBuffers
#    define glGenVertexArrays          wp_glGenVertexArrays
#    define glBindVertexArray          wp_glBindVertexArray
#    define glDeleteVertexArrays       wp_glDeleteVertexArrays

#  endif // _WIN32
#endif   // desktop

// ============================================================
// MVP retrieval — unified via gles2_get_mvp() on all platforms
// ============================================================
// The shim tracks our software matrix stack on every platform;
// desktop no longer queries the (removed) fixed-function stack.

static void get_current_mvp(float mvp[16]) {
    gles2_get_mvp(mvp);
}

// ============================================================
// Shader source
// ============================================================

#include <cmath>
#include <cstring>
#include <SDL.h>

// Desktop uses GLSL 1.50 Core (in/out, explicit frag output).
// GLES2 uses GLSL ES 1.00 (attribute/varying, gl_FragColor).

#ifdef DESKTOP_COMPAT_GL

static const char *WARP_VERT =
    "#version 150 core\n"
    "in  vec3 aWarpPos;\n"
    "uniform mat4 uWarpMVP;\n"
    "out vec4 vWarpClip;\n"
    "void main() {\n"
    "  vWarpClip   = uWarpMVP * vec4(aWarpPos, 1.0);\n"
    "  gl_Position = vWarpClip;\n"
    "}\n";

static const char *WARP_FRAG =
    "#version 150 core\n"
    "in  vec4      vWarpClip;\n"
    "uniform sampler2D uWarpTex;\n"
    "uniform vec2      uWarpCenterNDC;\n"
    "uniform vec2      uWarpRadiusNDC;\n"
    "uniform float     uWarpTime;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "  vec2  ndc   = vWarpClip.xy / vWarpClip.w;\n"
    "  vec2  d     = ndc - uWarpCenterNDC;\n"
    "  vec2  dNorm = d / uWarpRadiusNDC;\n"
    "  float t     = length(dNorm);\n"
    "  if (t > 1.0) discard;\n"
    "  vec2 dir = (t > 0.001) ? dNorm / t : vec2(0.0);\n"
    "  float ws = 0.55 * sin(t * 3.14159265)\n"
    "           + 0.04 * sin(t * 10.0 + uWarpTime) * (1.0 - t);\n"
    "  vec2 warpedNDC = ndc + dir * uWarpRadiusNDC * ws;\n"
    "  vec2 uv = clamp((warpedNDC + vec2(1.0)) * 0.5, 0.0, 1.0);\n"
    "  float edge = smoothstep(0.75, 1.0, t);\n"
    "  vec4 col   = texture(uWarpTex, uv);\n"
    "  fragColor  = vec4(col.rgb * (1.0 - 0.45 * edge), col.a);\n"
    "}\n";

#else // GLES2

#define WARP_PREC "precision mediump float;\n"

static const char *WARP_VERT =
    "attribute vec3 aWarpPos;\n"
    "uniform   mat4 uWarpMVP;\n"
    "varying   vec4 vWarpClip;\n"
    "void main() {\n"
    "  vWarpClip   = uWarpMVP * vec4(aWarpPos, 1.0);\n"
    "  gl_Position = vWarpClip;\n"
    "}\n";

static const char *WARP_FRAG =
    WARP_PREC
    "varying vec4      vWarpClip;\n"
    "uniform sampler2D uWarpTex;\n"
    "uniform vec2      uWarpCenterNDC;\n"
    "uniform vec2      uWarpRadiusNDC;\n"
    "uniform float     uWarpTime;\n"
    "void main() {\n"
    "  vec2  ndc   = vWarpClip.xy / vWarpClip.w;\n"
    "  vec2  d     = ndc - uWarpCenterNDC;\n"
    "  vec2  dNorm = d / uWarpRadiusNDC;\n"
    "  float t     = length(dNorm);\n"
    "  if (t > 1.0) discard;\n"
    "  vec2 dir = (t > 0.001) ? dNorm / t : vec2(0.0);\n"
    "  float ws = 0.55 * sin(t * 3.14159265)\n"
    "           + 0.04 * sin(t * 10.0 + uWarpTime) * (1.0 - t);\n"
    "  vec2 warpedNDC = ndc + dir * uWarpRadiusNDC * ws;\n"
    "  vec2 uv = clamp((warpedNDC + vec2(1.0)) * 0.5, 0.0, 1.0);\n"
    "  float edge = smoothstep(0.75, 1.0, t);\n"
    "  vec4 col   = texture2D(uWarpTex, uv);\n"
    "  gl_FragColor = vec4(col.rgb * (1.0 - 0.45 * edge), col.a);\n"
    "}\n";

#endif // DESKTOP_COMPAT_GL

// ============================================================
// Helpers
// ============================================================

static GLuint compile_warp_sh(GLenum type, const char *src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetShaderInfoLog(sh, (GLsizei)sizeof(buf), NULL, buf);
        SDL_Log("WarpPass shader compile error: %s", buf);
    }
    return sh;
}

// ============================================================
// WarpPass implementation
// ============================================================

WarpPass::WarpPass()
    : tex_(0), prog_(0), vbo_(0),
#ifdef DESKTOP_COMPAT_GL
      vao_(0),
#endif
      tex_w_(0), tex_h_(0),
      a_pos_(-1), u_mvp_(-1), u_tex_(-1),
      u_center_ndc_(-1), u_radius_ndc_(-1), u_time_(-1)
{
#ifdef _WIN32
    warp_load_gl_fns();
#endif

    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenBuffers(1, &vbo_);

#ifdef DESKTOP_COMPAT_GL
    glGenVertexArrays(1, &vao_);
#endif

    init_shader();
}

WarpPass::~WarpPass() {
    if (tex_)  glDeleteTextures(1, &tex_);
    if (prog_) glDeleteProgram(prog_);
    if (vbo_)  glDeleteBuffers(1, &vbo_);
#ifdef DESKTOP_COMPAT_GL
    if (vao_)  glDeleteVertexArrays(1, &vao_);
#endif
}

void WarpPass::init_shader() {
    GLuint vs = compile_warp_sh(GL_VERTEX_SHADER,   WARP_VERT);
    GLuint fs = compile_warp_sh(GL_FRAGMENT_SHADER, WARP_FRAG);
    prog_ = glCreateProgram();
    glAttachShader(prog_, vs);
    glAttachShader(prog_, fs);
    glLinkProgram(prog_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok; glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetProgramInfoLog(prog_, (GLsizei)sizeof(buf), NULL, buf);
        SDL_Log("WarpPass program link error: %s", buf);
    }

    a_pos_        = glGetAttribLocation (prog_, "aWarpPos");
    u_mvp_        = glGetUniformLocation(prog_, "uWarpMVP");
    u_tex_        = glGetUniformLocation(prog_, "uWarpTex");
    u_center_ndc_ = glGetUniformLocation(prog_, "uWarpCenterNDC");
    u_radius_ndc_ = glGetUniformLocation(prog_, "uWarpRadiusNDC");
    u_time_       = glGetUniformLocation(prog_, "uWarpTime");
}

void WarpPass::capture(int vp_x, int vp_y, int vp_w, int vp_h) {
    if (vp_w <= 0 || vp_h <= 0) return;
    glBindTexture(GL_TEXTURE_2D, tex_);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, vp_x, vp_y, vp_w, vp_h, 0);
    tex_w_ = vp_w;
    tex_h_ = vp_h;
    glBindTexture(GL_TEXTURE_2D, 0);
}

void WarpPass::draw(const Asteroid *a, float ax, float ay,
                    int /*vp_x*/, int /*vp_y*/,
                    int /*vp_w*/, int /*vp_h*/)
{
    if (!prog_ || !tex_w_ || !tex_h_) return;

    // ---- Get current MVP (Projection × ModelView) from the shim ----
    float mvp[16];
    get_current_mvp(mvp);

    // ---- Project asteroid centre to NDC ----
    float cw = mvp[3]*ax + mvp[7]*ay + mvp[15];
    if (fabsf(cw) < 1e-6f) return;
    float cx = (mvp[0]*ax + mvp[4]*ay + mvp[12]) / cw;
    float cy = (mvp[1]*ax + mvp[5]*ay + mvp[13]) / cw;

    // ---- Estimate NDC radii (per-axis for circular screen falloff) ----
    float ew = mvp[3]*(ax + a->radius) + mvp[7]*ay + mvp[15];
    if (fabsf(ew) < 1e-6f) ew = 1.0f;
    float ex = (mvp[0]*(ax + a->radius) + mvp[4]*ay + mvp[12]) / ew;
    float radius_ndc_x = fabsf(ex - cx);
    if (radius_ndc_x < 1e-5f) return;

    float eh = mvp[3]*ax + mvp[7]*(ay + a->radius) + mvp[15];
    if (fabsf(eh) < 1e-6f) eh = 1.0f;
    float ey = (mvp[1]*ax + mvp[5]*(ay + a->radius) + mvp[13]) / eh;
    float radius_ndc_y = fabsf(ey - cy);
    if (radius_ndc_y < 1e-5f) return;

    // ---- Build triangle-fan polygon matching the asteroid shape ----
    int segs = 5 + (int)(a->radius / 60.0f);
    if (segs > 9) segs = 9;
    const float step = 2.0f * (float)M_PI / (float)segs;
    const float rot  = a->rotation * (float)M_PI / 180.0f;

    // centre + segs perimeter points + 1 closing = segs+2 verts × 3 floats
    float vbuf[33];
    int nf = 0;
    vbuf[nf++] = ax; vbuf[nf++] = ay; vbuf[nf++] = 0.0f;
    for (int i = 0; i <= segs; i++) {
        float angle = rot + (float)(i % segs) * step;
        float off   = a->vertex_offsets[i % segs];
        vbuf[nf++] = ax + a->radius * off * cosf(angle);
        vbuf[nf++] = ay + a->radius * off * sinf(angle);
        vbuf[nf++] = 0.0f;
    }
    int num_verts = segs + 2;

    // ---- Upload vertex data to VBO ----
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(nf * sizeof(float)), vbuf, GL_STREAM_DRAW);

    // ---- Issue draw call ----
    glUseProgram(prog_);

#ifdef DESKTOP_COMPAT_GL
    glBindVertexArray(vao_);
#endif

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_);

    glUniform1i       (u_tex_,        0);
    glUniformMatrix4fv(u_mvp_,        1, GL_FALSE, mvp);
    glUniform2f       (u_center_ndc_, cx, cy);
    glUniform2f       (u_radius_ndc_, radius_ndc_x, radius_ndc_y);
    glUniform1f       (u_time_,       a->rotation * 0.01745329f);

    glEnableVertexAttribArray(a_pos_);
    glVertexAttribPointer(a_pos_, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLE_FAN, 0, num_verts);
    glDisableVertexAttribArray(a_pos_);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Restore the shim's program so subsequent shim draw calls work correctly.
    // On GLES2, gles2_compat calls glUseProgram(s_prog) before each draw
    // so no explicit reset is needed there — but it doesn't hurt.
    glUseProgram(0);
}
