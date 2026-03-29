// On Linux desktop, GL_GLEXT_PROTOTYPES must be defined before any OpenGL
// header is included so that <GL/glext.h> declares the OpenGL 2.0+ shader
// functions (glCreateShader, glUseProgram, etc.).
#if !defined(__ANDROID__) && !defined(__IOS__) && !defined(__EMSCRIPTEN__)
#  if defined(__linux__)
#    ifndef GL_GLEXT_PROTOTYPES
#      define GL_GLEXT_PROTOTYPES
#    endif
#  endif
#endif

#include "warp_pass.h"

// After gl_compat.h has pulled in the platform GL headers, include glext.h on
// Linux to get the OpenGL 2.0+ shader-API declarations.
#if !defined(__ANDROID__) && !defined(__IOS__) && !defined(__EMSCRIPTEN__)
#  if defined(__linux__)
#    include <GL/glext.h>
#  endif
#endif

#include <cmath>
#include <cstring>
#include <SDL.h>

// ============================================================
// Platform-specific MVP retrieval
// ============================================================

#if defined(__ANDROID__) || defined(__IOS__) || defined(__EMSCRIPTEN__)
// GLES2: matrix state lives inside gles2_compat – query it directly.
#include "gles2_compat.h"
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

// GLES2 fragment shaders require an explicit precision qualifier; desktop
// GLSL (version 110/120) does not support the keyword at all.
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
// For a fragment at normalised distance t = dist/radius from the asteroid
// centre (t ∈ [0,1]):
//
//   warp = 0.55 * sin(t·π) * radius_ndc
//        + 0.04 * sin(t·10 + rotation) * (1−t) * radius_ndc   ← ripple
//
// The sample coordinate is displaced *outward* by `warp` from the asteroid
// centre.  At t ≈ 0.5 the outward shift exceeds the remaining distance to
// the edge (0.5·r + 0.55·r ≈ 1.05·r), so we sample from *outside* the
// asteroid – pulling visible stars and game objects inward into the lens.
// The centre (t≈0) and edge (t≈1) have near-zero warp so they stay dark,
// producing the classic black-hole shadow + Einstein-ring appearance.
//
// A smooth edge darkening hints at the asteroid boundary even in empty space.
static const char *WARP_FRAG =
    WARP_PREC
    "varying vec4      vWarpClip;\n"
    "uniform sampler2D uWarpTex;\n"
    "uniform vec2      uWarpCenterNDC;\n"
    "uniform float     uWarpRadiusNDC;\n"
    "uniform float     uWarpTime;\n"       // asteroid rotation (radians)
    "void main() {\n"
    "  vec2  ndc  = vWarpClip.xy / vWarpClip.w;\n"
    "  vec2  d    = ndc - uWarpCenterNDC;\n"
    "  float dist = length(d);\n"
    "  float t    = dist / uWarpRadiusNDC;\n"
    "  if (t > 1.0) discard;\n"
    // Outward radial warp: peaks at midpoint, zero at centre and edge.
    "  float warp  = 0.55 * sin(t * 3.14159265) * uWarpRadiusNDC;\n"
    // Subtle time-based ripple (uses asteroid rotation as a clock).
    "  warp += 0.04 * sin(t * 10.0 + uWarpTime) * (1.0 - t) * uWarpRadiusNDC;\n"
    "  vec2 dir = (dist > 0.001) ? d / dist : vec2(0.0);\n"
    "  vec2 warpedNDC = ndc + dir * warp;\n"
    // NDC [-1,1] → texture UV [0,1].  The texture covers the viewport exactly.
    "  vec2 uv = clamp((warpedNDC + vec2(1.0)) * 0.5, 0.0, 1.0);\n"
    // Dim the edge to hint at the asteroid boundary.
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

    // ---- Estimate NDC radius by projecting a point on the edge ----
    float ew = mvp[3]*(ax + a->radius) + mvp[7]*ay + mvp[15];
    if (fabsf(ew) < 1e-6f) ew = 1.0f;
    float ex = (mvp[0]*(ax + a->radius) + mvp[4]*ay + mvp[12]) / ew;
    float radius_ndc = fabsf(ex - cx);
    if (radius_ndc < 1e-5f) return;

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
    glUniform1f       (u_radius_ndc_, radius_ndc);
    // Use the asteroid's rotation (converted to radians) as a time value so
    // the ripple animates as the asteroid spins.
    glUniform1f       (u_time_,       a->rotation * 0.01745329f);

    glEnableVertexAttribArray(a_pos_);
    glVertexAttribPointer(a_pos_, 3, GL_FLOAT, GL_FALSE, 0, vbuf);
    glDrawArrays(GL_TRIANGLE_FAN, 0, num_verts);
    glDisableVertexAttribArray(a_pos_);

    glBindTexture(GL_TEXTURE_2D, 0);
    // Leave glUseProgram as-is: the gles2_compat layer (or fixed-function
    // fallback on desktop) will restore its own program on the next draw call.
}
