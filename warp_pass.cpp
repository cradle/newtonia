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
//   macOS   – <GLUT/glut.h> already pulls in OpenGL up to 4.1;
//             no extra work needed.
//   GLES2   – shader API is core; gles2_compat.h provides the
//             necessary MVP accessor.

#if !defined(__ANDROID__) && !defined(__IOS__) && !defined(__EMSCRIPTEN__)
#  if defined(__linux__)
#    ifndef GL_GLEXT_PROTOTYPES
#      define GL_GLEXT_PROTOTYPES
#    endif
#  endif
#endif

#include "warp_pass.h"   // pulls in gl_compat.h → platform GL/GLUT headers

// Post-GL-header includes and Windows function loader.
#if !defined(__ANDROID__) && !defined(__IOS__) && !defined(__EMSCRIPTEN__)
#  if defined(__linux__)
     // GL_GLEXT_PROTOTYPES was set before the GL headers were included;
     // glext.h now adds declarations for all extension functions.
#    include <GL/glext.h>

#  elif defined(_WIN32)
     // Include glext.h for constants (GL_VERTEX_SHADER, GL_CLAMP_TO_EDGE,
     // GL_TEXTURE0 …) and PFN* typedefs. Do NOT define GL_GLEXT_PROTOTYPES
     // here – opengl32.dll doesn't export those symbols and the linker
     // would error.
#    include <GL/glext.h>

     // Table of every GL 2.0+ function this file calls.
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
       X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray)

     // Declare file-scope function pointers (wp_ prefix avoids collisions).
#    define X(T, name) static T wp_##name = NULL;
     WARP_GL_FNS
#    undef X

     // Load all pointers from the current OpenGL context.
     static void warp_load_gl_fns() {
#      define X(T, name) wp_##name = (T)wglGetProcAddress(#name);
       WARP_GL_FNS
#      undef X
     }

     // Redirect the standard GL names to our runtime-loaded pointers
     // for all code below this point in this translation unit.
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

#  endif // _WIN32
#endif   // desktop

// ============================================================
// Platform-specific MVP retrieval
// ============================================================

#if defined(__ANDROID__) || defined(__IOS__) || defined(__EMSCRIPTEN__)
// GLES2: matrix state lives inside gles2_compat – query it directly.
#  include "gles2_compat.h"
static void get_current_mvp(float mvp[16]) {
    gles2_get_mvp(mvp);
}
#else
// Desktop OpenGL: read the native matrix stack and multiply.
static void mat4_mul16(float *res, const float *a, const float *b) {
    // Column-major: res[col*4+row] = sum_k a[k*4+row] * b[col*4+k]
    for (int col = 0; col < 4; col++)
        for (int row = 0; row < 4; row++) {
            float s = 0;
            for (int k = 0; k < 4; k++)
                s += a[k*4 + row] * b[col*4 + k];
            res[col*4 + row] = s;
        }
}
static void get_current_mvp(float mvp[16]) {
    float mv[16], proj[16];
    glGetFloatv(GL_MODELVIEW_MATRIX,  mv);
    glGetFloatv(GL_PROJECTION_MATRIX, proj);
    mat4_mul16(mvp, proj, mv);  // MVP = Projection * ModelView
}
#endif

// ============================================================
// Shader source
// ============================================================

#include <cmath>
#include <cstring>
#include <SDL.h>

// GLES2 fragment shaders require an explicit precision qualifier;
// desktop GLSL (version 110/120) does not support the keyword at all.
#if defined(__ANDROID__) || defined(__IOS__) || defined(__EMSCRIPTEN__)
#  define WARP_PREC "precision mediump float;\n"
#else
#  define WARP_PREC ""
#endif

static const char *WARP_VERT =
    "attribute vec3 aWarpPos;\n"
    "uniform   mat4 uWarpMVP;\n"
    "varying   vec4 vWarpClip;\n"
    "void main() {\n"
    "  vWarpClip   = uWarpMVP * vec4(aWarpPos, 1.0);\n"
    "  gl_Position = vWarpClip;\n"
    "}\n";

// Distortion model
// ─────────────────
// uWarpRadiusNDC is now a vec2 (rx, ry): the NDC extents of the asteroid
// radius measured horizontally and vertically.  Because perspective maps a
// world-space circle to a taller NDC ellipse on landscape screens (the Y
// NDC range covers fewer pixels per unit than X), using a single scalar
// radius produced an elliptical warp effect on screen.  Using per-axis
// radii to normalise the distance calculation makes the falloff circular
// in screen pixels, matching the actual asteroid shape.
//
// The warp displaces samples *outward* from the asteroid centre.  At t≈0.5
// the shift exceeds the remaining distance to the edge, pulling exterior
// scene content inward to create a gravitational-lens / Einstein-ring look.
static const char *WARP_FRAG =
    WARP_PREC
    "varying vec4      vWarpClip;\n"
    "uniform sampler2D uWarpTex;\n"
    "uniform vec2      uWarpCenterNDC;\n"
    "uniform vec2      uWarpRadiusNDC;\n"   // (rx, ry): per-axis NDC radii
    "uniform float     uWarpTime;\n"
    "void main() {\n"
    "  vec2  ndc   = vWarpClip.xy / vWarpClip.w;\n"
    "  vec2  d     = ndc - uWarpCenterNDC;\n"
    // Normalise per-axis so t=1 at the circular asteroid boundary in screen pixels.
    "  vec2  dNorm = d / uWarpRadiusNDC;\n"
    "  float t     = length(dNorm);\n"
    "  if (t > 1.0) discard;\n"
    // Direction in screen-circular space, then back to NDC for the displacement.
    "  vec2 dir = (t > 0.001) ? dNorm / t : vec2(0.0);\n"
    "  float ws = 0.55 * sin(t * 3.14159265)\n"
    "           + 0.04 * sin(t * 10.0 + uWarpTime) * (1.0 - t);\n"
    "  vec2 warpedNDC = ndc + dir * uWarpRadiusNDC * ws;\n"
    "  vec2 uv = clamp((warpedNDC + vec2(1.0)) * 0.5, 0.0, 1.0);\n"
    "  float edge = smoothstep(0.75, 1.0, t);\n"
    "  vec4 col   = texture2D(uWarpTex, uv);\n"
    "  gl_FragColor = vec4(col.rgb * (1.0 - 0.45 * edge), col.a);\n"
    "}\n";

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
    : tex_(0), prog_(0), tex_w_(0), tex_h_(0),
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

    init_shader();
}

WarpPass::~WarpPass() {
    if (tex_)  glDeleteTextures(1, &tex_);
    if (prog_) glDeleteProgram(prog_);
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

    // ---- Get current MVP (Projection × ModelView) ----
    float mvp[16];
    get_current_mvp(mvp);

    // ---- Project asteroid centre to NDC ----
    // MVP is column-major; multiply MVP * (ax, ay, 0, 1).
    float cw = mvp[3]*ax + mvp[7]*ay + mvp[15];
    if (fabsf(cw) < 1e-6f) return;
    float cx = (mvp[0]*ax + mvp[4]*ay + mvp[12]) / cw;
    float cy = (mvp[1]*ax + mvp[5]*ay + mvp[13]) / cw;

    // ---- Estimate NDC radii by projecting edge points in X and Y ----
    // The asteroid is circular in world space but the perspective projection
    // maps it to a taller NDC ellipse on landscape screens (Y NDC covers
    // fewer pixels per unit than X).  Both radii are needed so the shader
    // can normalise per-axis and produce a circular falloff in screen pixels.
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
    // seg count mirrors AsteroidDrawer::seg_count(): 5 + radius/60, capped at 9.
    int segs = 5 + (int)(a->radius / 60.0f);
    if (segs > 9) segs = 9;
    const float step = 2.0f * (float)M_PI / (float)segs;
    const float rot  = a->rotation * (float)M_PI / 180.0f;

    // Vertices: centre + segs perimeter points + 1 closing point = segs+2.
    // Maximum: segs=9 → 11 vertices × 3 floats = 33 floats.
    float vbuf[33];
    int nf = 0;
    vbuf[nf++] = ax; vbuf[nf++] = ay; vbuf[nf++] = 0.0f;  // centre
    for (int i = 0; i <= segs; i++) {
        float angle = rot + (float)(i % segs) * step;
        float off   = a->vertex_offsets[i % segs];
        vbuf[nf++] = ax + a->radius * off * cosf(angle);
        vbuf[nf++] = ay + a->radius * off * sinf(angle);
        vbuf[nf++] = 0.0f;
    }
    int num_verts = segs + 2;

    // ---- Issue draw call ----
    glUseProgram(prog_);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_);

    glUniform1i       (u_tex_,        0);
    glUniformMatrix4fv(u_mvp_,        1, GL_FALSE, mvp);
    glUniform2f       (u_center_ndc_, cx, cy);
    glUniform2f       (u_radius_ndc_, radius_ndc_x, radius_ndc_y);
    // Use the asteroid's rotation (in radians) as a time value so
    // the ripple animates as the asteroid spins.
    glUniform1f       (u_time_,       a->rotation * 0.01745329f);

    glEnableVertexAttribArray(a_pos_);
    glVertexAttribPointer(a_pos_, 3, GL_FLOAT, GL_FALSE, 0, vbuf);
    glDrawArrays(GL_TRIANGLE_FAN, 0, num_verts);
    glDisableVertexAttribArray(a_pos_);

    glBindTexture(GL_TEXTURE_2D, 0);
#if !defined(__ANDROID__) && !defined(__IOS__) && !defined(__EMSCRIPTEN__)
    // Restore the fixed-function pipeline.  Desktop OpenGL does not clear the
    // bound program automatically when switching back to glBegin/glEnd calls,
    // so the warp shader would remain active for all subsequent rendering.
    // On GLES2 targets, gles2_compat explicitly calls glUseProgram(s_prog)
    // before each draw, so no reset is needed there.
    glUseProgram(0);
#endif
}
