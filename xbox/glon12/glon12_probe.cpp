// GLon12 desktop rendering spike — Newtonia Xbox port, Phase 2 (work-item 3a).
//
// Question this answers (no GDKX, no dev kit required):
//   Does GLon12 (Mesa's OpenGL-on-D3D12 Gallium driver) expose a GL version and
//   feature set that covers Newtonia's GL 3.3 core renderer, and does that
//   renderer's GLSL-330 program actually compile, link, and rasterise through it?
//
// It runs through whatever opengl32.dll the OS loader resolves: drop Mesa's
// desktop GLon12 build (opengl32.dll + libgallium_wgl.dll + dxil.dll) next to
// this exe and the WGL context becomes a D3D12-backed GLon12 context. With no
// DLLs alongside it falls back to the system/hardware GL driver — useful as a
// baseline. See xbox/GLON12_SPIKE.md.
//
// This deliberately mirrors the console path the port will take (SDL2 WGL via
// SDL_GL_CreateContext + SDL_GL_SwapWindow, GL 3.3 core context, the same set
// of GL entry points the renderer loads in gles2_compat.cpp's COMPAT_GL_FNS),
// NOT the current ANGLE/EGL-pbuffer Desktop path.
//
// Usage:
//   glon12_probe [--frames N] [--hidden] [--no-window-loop]
//     --frames N        render N frames then exit (default 1 in CI/hidden mode,
//                        else loops until the window is closed)
//     --hidden          create a hidden window (headless CI); still a real WGL
//                        context + swap, so D3D12/WARP is exercised
//     --no-window-loop   alias for --hidden --frames 3
//
// Exit code 0 = GL >= 3.3 core, GLSL >= 330, all required entry points present,
// shader program compiled+linked, and the rendered frame was non-empty.
// Non-zero = a specific check failed (logged to stdout AND glon12_probe.log).

#define SDL_MAIN_HANDLED
#include <SDL.h>

// We only need the GL types + tokens here; entry points are loaded by hand via
// SDL_GL_GetProcAddress so this stays driver-agnostic (exactly how the console
// SDL path resolves them). Pull in the platform GL header just for types.
#if defined(_WIN32)
#  include <windows.h>
#  undef near
#  undef far
#  include <GL/gl.h>
#  include <GL/glext.h>
#elif defined(__APPLE__)
#  include <OpenGL/gl3.h>
#  include <OpenGL/gl3ext.h>
#else
#  include <GL/gl.h>
#  include <GL/glext.h>
#endif

#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

// ------------------------------------------------------------------
// Logging — tee to stdout and glon12_probe.log next to the exe.
// ------------------------------------------------------------------
static FILE *g_log = nullptr;
static void logf(const char *fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fputs(buf, stdout);
    fputc('\n', stdout);
    fflush(stdout);
    if (g_log) { fputs(buf, g_log); fputc('\n', g_log); fflush(g_log); }
}

// ------------------------------------------------------------------
// GL entry points the renderer needs. The 2.0+ list mirrors COMPAT_GL_FNS in
// gles2_compat.cpp (the functions Newtonia loads via wglGetProcAddress on
// Windows today); if any of these is null on GLon12, the renderer can't run.
// ------------------------------------------------------------------
#define REQUIRED_GL_FNS \
    X(PFNGLCREATESHADERPROC,             glCreateShader) \
    X(PFNGLSHADERSOURCEPROC,             glShaderSource) \
    X(PFNGLCOMPILESHADERPROC,            glCompileShader) \
    X(PFNGLGETSHADERIVPROC,              glGetShaderiv) \
    X(PFNGLGETSHADERINFOLOGPROC,         glGetShaderInfoLog) \
    X(PFNGLCREATEPROGRAMPROC,            glCreateProgram) \
    X(PFNGLATTACHSHADERPROC,             glAttachShader) \
    X(PFNGLLINKPROGRAMPROC,              glLinkProgram) \
    X(PFNGLDELETESHADERPROC,             glDeleteShader) \
    X(PFNGLDELETEPROGRAMPROC,            glDeleteProgram) \
    X(PFNGLGETPROGRAMIVPROC,             glGetProgramiv) \
    X(PFNGLGETPROGRAMINFOLOGPROC,        glGetProgramInfoLog) \
    X(PFNGLGETATTRIBLOCATIONPROC,        glGetAttribLocation) \
    X(PFNGLGETUNIFORMLOCATIONPROC,       glGetUniformLocation) \
    X(PFNGLUSEPROGRAMPROC,               glUseProgram) \
    X(PFNGLUNIFORM1IPROC,                glUniform1i) \
    X(PFNGLUNIFORM1FPROC,                glUniform1f) \
    X(PFNGLUNIFORM4FPROC,                glUniform4f) \
    X(PFNGLUNIFORMMATRIX4FVPROC,         glUniformMatrix4fv) \
    X(PFNGLGENBUFFERSPROC,               glGenBuffers) \
    X(PFNGLBINDBUFFERPROC,               glBindBuffer) \
    X(PFNGLBUFFERDATAPROC,               glBufferData) \
    X(PFNGLDELETEBUFFERSPROC,            glDeleteBuffers) \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC,  glEnableVertexAttribArray) \
    X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray) \
    X(PFNGLVERTEXATTRIBPOINTERPROC,      glVertexAttribPointer) \
    X(PFNGLGENVERTEXARRAYSPROC,          glGenVertexArrays) \
    X(PFNGLBINDVERTEXARRAYPROC,          glBindVertexArray) \
    X(PFNGLDELETEVERTEXARRAYSPROC,       glDeleteVertexArrays)

#define X(T, name) static T name = nullptr;
REQUIRED_GL_FNS
#undef X

// Total number of required entry points (for the report).
static int required_fn_count()
{
    int n = 0;
#define X(T, name) ++n;
    REQUIRED_GL_FNS
#undef X
    return n;
}

// Load every required entry point; returns the count of MISSING ones (0 = good)
// and fills `missing` with their names for the report.
static int load_required_fns(std::vector<std::string> &missing)
{
#define X(T, name) \
    name = (T)SDL_GL_GetProcAddress(#name); \
    if (!name) missing.push_back(#name);
    REQUIRED_GL_FNS
#undef X
    return (int)missing.size();
}

// ------------------------------------------------------------------
// The renderer's program is GLSL 330 core with position + colour attributes,
// an MVP uniform, and a tint uniform (see gles2_compat.cpp's desktop shader).
// We use a compact but representative version: if this compiles, links, and
// draws on GLon12, the real program's feature requirements are met.
// ------------------------------------------------------------------
static const char *VERT_SRC =
    "#version 330 core\n"
    "layout(location=0) in vec3 a_pos;\n"
    "layout(location=1) in vec4 a_col;\n"
    "uniform mat4 u_mvp;\n"
    "out vec4 v_col;\n"
    "void main(){ v_col = a_col; gl_Position = u_mvp * vec4(a_pos,1.0); }\n";

static const char *FRAG_SRC =
    "#version 330 core\n"
    "in vec4 v_col;\n"
    "uniform vec4 u_tint;\n"
    "out vec4 o_col;\n"
    "void main(){ o_col = v_col * u_tint; }\n";

static bool compile_shader(GLenum type, const char *src, GLuint &out, std::string &err)
{
    out = glCreateShader(type);
    glShaderSource(out, 1, &src, nullptr);
    glCompileShader(out);
    GLint ok = 0;
    glGetShaderiv(out, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[1024] = {0};
        glGetShaderInfoLog(out, sizeof(buf), nullptr, buf);
        err = buf;
        return false;
    }
    return true;
}

int main(int argc, char *argv[])
{
    int  want_frames = -1;   // -1 = "loop until closed" (interactive default)
    bool hidden      = false;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) want_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--hidden")) hidden = true;
        else if (!strcmp(argv[i], "--no-window-loop")) { hidden = true; if (want_frames < 0) want_frames = 3; }
    }
    if (hidden && want_frames < 0) want_frames = 1;

    g_log = fopen("glon12_probe.log", "w");

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        logf("FAIL: SDL_Init: %s", SDL_GetError());
        return 2;
    }

    // Request a GL 3.3 core context — the renderer's target profile. This is
    // the same request the console SDL2 WGL path will make.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);

    const int W = 640, H = 480;
    Uint32 flags = SDL_WINDOW_OPENGL | (hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN);
    SDL_Window *win = SDL_CreateWindow("Newtonia GLon12 probe",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       W, H, flags);
    if (!win) {
        logf("FAIL: SDL_CreateWindow: %s", SDL_GetError());
        SDL_Quit();
        return 2;
    }

    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) {
        logf("FAIL: SDL_GL_CreateContext (no GL 3.3 core context): %s", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 3;
    }
    SDL_GL_MakeCurrent(win, ctx);

    // ---- Report the driver behind this context ----
    const char *vendor   = (const char *)glGetString(GL_VENDOR);
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    const char *version  = (const char *)glGetString(GL_VERSION);
    const char *glsl     = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
    logf("GL_VENDOR   : %s", vendor   ? vendor   : "(null)");
    logf("GL_RENDERER : %s", renderer ? renderer : "(null)");
    logf("GL_VERSION  : %s", version  ? version  : "(null)");
    logf("GL_SL_VER   : %s", glsl     ? glsl     : "(null)");

    bool is_glon12 = renderer && (strstr(renderer, "D3D12") || strstr(renderer, "Direct3D12") ||
                                  strstr(renderer, "d3d12"));
    logf("Backend     : %s", is_glon12 ? "GLon12 (D3D12)" :
                             "system/other GL (no GLon12 DLLs found alongside exe)");

    int rc = 0;

    // ---- Check 1: context GL version >= 3.3 ----
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    logf("Context GL version: %d.%d", major, minor);
    if (major < 3 || (major == 3 && minor < 3)) {
        logf("FAIL: need GL >= 3.3 core, got %d.%d", major, minor);
        rc = 4;
    }

    // ---- Check 2: every required GL entry point resolves ----
    std::vector<std::string> missing;
    int n_missing = load_required_fns(missing);
    if (n_missing) {
        logf("FAIL: %d of %d required GL entry point(s) missing:",
             n_missing, required_fn_count());
        for (const auto &m : missing) logf("    - %s", m.c_str());
        rc = rc ? rc : 5;
    } else {
        logf("All %d required GL entry points resolved.", required_fn_count());
    }

    // ---- Check 3: the GLSL 330 program compiles, links, and a frame draws ----
    if (!n_missing && rc == 0) {
        GLuint vs = 0, fs = 0, prog = 0;
        std::string err;
        if (!compile_shader(GL_VERTEX_SHADER, VERT_SRC, vs, err)) {
            logf("FAIL: vertex shader compile: %s", err.c_str());
            rc = 6;
        } else if (!compile_shader(GL_FRAGMENT_SHADER, FRAG_SRC, fs, err)) {
            logf("FAIL: fragment shader compile: %s", err.c_str());
            rc = 6;
        } else {
            prog = glCreateProgram();
            glAttachShader(prog, vs);
            glAttachShader(prog, fs);
            glLinkProgram(prog);
            GLint linked = 0;
            glGetProgramiv(prog, GL_LINK_STATUS, &linked);
            if (!linked) {
                char buf[1024] = {0};
                glGetProgramInfoLog(prog, sizeof(buf), nullptr, buf);
                logf("FAIL: program link: %s", buf);
                rc = 7;
            }
        }

        if (rc == 0) {
            // VAO + interleaved pos(3)+col(4) VBO — the Mesh layout.
            GLuint vao = 0, vbo = 0;
            glGenVertexArrays(1, &vao);
            glBindVertexArray(vao);
            glGenBuffers(1, &vbo);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            const float verts[] = {
                //  x      y     z     r    g    b    a
                -0.6f, -0.5f, 0.0f, 1.0f, 0.2f, 0.2f, 1.0f,
                 0.6f, -0.5f, 0.0f, 0.2f, 1.0f, 0.2f, 1.0f,
                 0.0f,  0.6f, 0.0f, 0.2f, 0.2f, 1.0f, 1.0f,
            };
            glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void *)0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void *)(3 * sizeof(float)));

            glUseProgram(prog);
            const float ident[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
            GLint u_mvp  = glGetUniformLocation(prog, "u_mvp");
            GLint u_tint = glGetUniformLocation(prog, "u_tint");
            glUniformMatrix4fv(u_mvp, 1, GL_FALSE, ident);
            glUniform4f(u_tint, 1.0f, 1.0f, 1.0f, 1.0f);

            int frames = want_frames < 0 ? 1 : want_frames;
            bool nonempty = false;
            bool quit = false;
            int drawn = 0;
            while (!quit) {
                SDL_Event e;
                while (SDL_PollEvent(&e)) {
                    if (e.type == SDL_QUIT ||
                        (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE))
                        quit = true;
                }
                glViewport(0, 0, W, H);
                glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                glBindVertexArray(vao);
                glDrawArrays(GL_TRIANGLES, 0, 3);

                // After the first draw, read back the centre pixel: a non-clear
                // colour proves GLon12 actually rasterised our geometry.
                if (drawn == 0) {
                    unsigned char px[4] = {0,0,0,0};
                    glReadPixels(W / 2, H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
                    GLenum gerr = glGetError();
                    logf("Centre pixel after draw: R=%d G=%d B=%d A=%d (glGetError=0x%x)",
                         px[0], px[1], px[2], px[3], gerr);
                    // Clear colour is ~ (13,13,20). The screen centre lies inside
                    // the triangle, so a successfully rasterised frame yields a
                    // markedly brighter pixel and no GL error.
                    nonempty = (px[0] >= 24 || px[1] >= 24 || px[2] >= 40) &&
                               gerr == GL_NO_ERROR;
                }

                SDL_GL_SwapWindow(win);
                ++drawn;
                if (want_frames >= 0 && drawn >= frames) quit = true;
                if (!hidden && want_frames < 0) { /* interactive: loop forever */ }
            }

            if (!nonempty) {
                logf("FAIL: rendered frame appears empty (no triangle rasterised).");
                rc = 8;
            } else {
                logf("Triangle rasterised through this GL backend. Frames drawn: %d", drawn);
            }

            glDeleteProgram(prog);
            glDeleteBuffers(1, &vbo);
            glDeleteVertexArrays(1, &vao);
        }
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
    }

    logf("RESULT: %s (exit %d)", rc == 0 ? "PASS" : "FAIL", rc);

    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    if (g_log) fclose(g_log);
    return rc;
}
