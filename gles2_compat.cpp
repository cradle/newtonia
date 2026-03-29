// OpenGL ES 2.0 compatibility shim implementation.
// Provides a subset of the OpenGL 1.x fixed-function pipeline using GLES2
// shaders + vertex arrays.

#include "gles2_compat.h"
// Undef our viewport macro so gles2_set_viewport can call the native function
// without recursing, and so replay_list's glViewport call reaches GLES2 directly.
#ifdef glViewport
#  undef glViewport
#endif

#include <SDL.h>

#include <cmath>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <map>

// ============================================================
// Internal: 4×4 column-major matrix helpers
// ============================================================

typedef float mat4[16];

static void mat4_identity(mat4 m) {
    memset(m, 0, sizeof(float)*16);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat4_multiply(mat4 result, const mat4 a, const mat4 b) {
    mat4 tmp;
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            float s = 0;
            for (int k = 0; k < 4; k++)
                s += a[k*4 + row] * b[col*4 + k];
            tmp[col*4 + row] = s;
        }
    }
    memcpy(result, tmp, sizeof(float)*16);
}

static void mat4_translate_inplace(mat4 m, float x, float y, float z) {
    mat4 t; mat4_identity(t);
    t[12] = x; t[13] = y; t[14] = z;
    mat4_multiply(m, m, t);
}

static void mat4_scale_inplace(mat4 m, float x, float y, float z) {
    mat4 s; mat4_identity(s);
    s[0] = x; s[5] = y; s[10] = z;
    mat4_multiply(m, m, s);
}

static void mat4_rotate_inplace(mat4 m, float angle_deg, float x, float y, float z) {
    float a = angle_deg * (float)M_PI / 180.0f;
    float c = cosf(a), s = sinf(a);
    float len = sqrtf(x*x + y*y + z*z);
    if (len > 0.0f) { x /= len; y /= len; z /= len; }

    mat4 r; mat4_identity(r);
    r[0]  = c + x*x*(1-c);       r[4]  = x*y*(1-c) - z*s;    r[8]  = x*z*(1-c) + y*s;
    r[1]  = y*x*(1-c) + z*s;     r[5]  = c + y*y*(1-c);       r[9]  = y*z*(1-c) - x*s;
    r[2]  = z*x*(1-c) - y*s;     r[6]  = z*y*(1-c) + x*s;     r[10] = c + z*z*(1-c);
    mat4_multiply(m, m, r);
}

static void mat4_ortho(mat4 m,
                       float l, float r, float b, float t, float n, float f) {
    mat4_identity(m);
    m[0]  =  2.0f / (r - l);
    m[5]  =  2.0f / (t - b);
    m[10] = -2.0f / (f - n);
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[14] = -(f + n) / (f - n);
}

static void mat4_perspective(mat4 m,
                             float fovy_deg, float aspect, float near, float far) {
    float f = 1.0f / tanf(fovy_deg * (float)M_PI / 360.0f);
    memset(m, 0, sizeof(float)*16);
    m[0]  = f / aspect;
    m[5]  = f;
    m[10] = (far + near) / (near - far);
    m[11] = -1.0f;
    m[14] = 2.0f * far * near / (near - far);
}

static void mat4_lookat(mat4 m,
                        float ex, float ey, float ez,
                        float cx, float cy, float cz,
                        float ux, float uy, float uz) {
    float fx = cx - ex, fy = cy - ey, fz = cz - ez;
    float fl = sqrtf(fx*fx + fy*fy + fz*fz);
    fx /= fl; fy /= fl; fz /= fl;

    float sx = fy*uz - fz*uy, sy = fz*ux - fx*uz, sz = fx*uy - fy*ux;
    float sl = sqrtf(sx*sx + sy*sy + sz*sz);
    sx /= sl; sy /= sl; sz /= sl;

    float rx = sy*fz - sz*fy, ry = sz*fx - sx*fz, rz = sx*fy - sy*fx;

    mat4 view;
    mat4_identity(view);
    view[0] = sx; view[4] = sy; view[8]  = sz;
    view[1] = rx; view[5] = ry; view[9]  = rz;
    view[2] =-fx; view[6] =-fy; view[10] =-fz;
    view[12] = -(sx*ex + sy*ey + sz*ez);
    view[13] = -(rx*ex + ry*ey + rz*ez);
    view[14] =  (fx*ex + fy*ey + fz*ez);
    mat4_multiply(m, view, m);
}

// ============================================================
// Internal: matrix stacks
// ============================================================

static const int MAX_STACK_DEPTH = 32;

struct MatrixStack {
    mat4  stack[MAX_STACK_DEPTH];
    int   top = 0;
    MatrixStack() { mat4_identity(stack[0]); }
};

static MatrixStack s_modelview;
static MatrixStack s_projection;
static GLenum      s_matrix_mode = GL_MODELVIEW;

static MatrixStack& current_stack() {
    return (s_matrix_mode == GL_PROJECTION) ? s_projection : s_modelview;
}
static float* current_matrix() {
    MatrixStack &st = current_stack();
    return st.stack[st.top];
}

// ============================================================
// Internal: vertex buffer
// ============================================================

struct Vertex {
    float x, y, z;
    float r, g, b, a;
};

static float         s_color[4]     = {1,1,1,1};
static float         s_point_size   = 1.0f;
static float         s_line_width   = 1.0f;
static GLint         s_viewport[4]  = {0, 0, 800, 600}; // cached; avoids glGetIntegerv in hot path
static bool          s_in_begin     = false;
static GLenum        s_begin_mode   = GL_POINTS;
static std::vector<Vertex> s_vbuf;

// ============================================================
// Internal: display lists
// ============================================================

struct DLCommand {
    enum Type {
        BEGIN, END,
        VERTEX3F,
        COLOR4F,
        PUSH_MATRIX, POP_MATRIX,
        LOAD_IDENTITY, MATRIX_MODE,
        TRANSLATEF, ROTATEF, SCALEF,
        ORTHO,
        PERSPECTIVE,
        LOOKAT,
        POINT_SIZE,
        LINE_WIDTH,
        CALL_LIST,
        CLEAR, CLEAR_COLOR,
        VIEWPORT
    } type;
    float f[9];
    int   i[4];
};

struct DisplayList { std::vector<DLCommand> cmds; };

static std::map<GLuint, DisplayList> s_lists;
static GLuint  s_next_id     = 1;
static GLuint  s_recording   = 0;   // 0 = not recording
static bool    s_is_recording = false;

static void dl_push(DLCommand cmd) {
    s_lists[s_recording].cmds.push_back(cmd);
}

// ============================================================
// Internal: GLES2 shader program
// ============================================================

static GLuint s_prog = 0;
static GLint  s_attr_pos   = -1;
static GLint  s_attr_color = -1;
static GLint  s_uni_mvp    = -1;
static GLint  s_uni_ptsz   = -1;
static GLint  s_uni_ispt   = -1;
static GLuint s_vbo_pos    = 0;
static GLuint s_vbo_col    = 0;

static const char *VERT_SRC =
    "attribute vec3 aPos;\n"
    "attribute vec4 aCol;\n"
    "uniform   mat4 uMVP;\n"
    "uniform  float uPointSize;\n"
    "varying   vec4 vCol;\n"
    "void main(){\n"
    "  gl_Position  = uMVP * vec4(aPos, 1.0);\n"
    "  gl_PointSize = uPointSize;\n"
    "  vCol = aCol;\n"
    "}\n";

static const char *FRAG_SRC =
    "precision mediump float;\n"
    "varying vec4 vCol;\n"
    "uniform int uIsPoint;\n"
    "void main(){\n"
    "  if(uIsPoint != 0){\n"
    "    vec2 pc = gl_PointCoord - vec2(0.5);\n"
    "    if(dot(pc,pc) > 0.25) discard;\n"
    "  }\n"
    "  gl_FragColor = vCol;\n"
    "}\n";

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512]; glGetShaderInfoLog(sh, sizeof(buf), NULL, buf);
        SDL_Log("Shader compile error: %s", buf);
    }
    return sh;
}

static void init_shader() {
    GLuint vs = compile_shader(GL_VERTEX_SHADER,   VERT_SRC);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, FRAG_SRC);
    s_prog = glCreateProgram();
    glAttachShader(s_prog, vs);
    glAttachShader(s_prog, fs);
    glLinkProgram(s_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    s_attr_pos   = glGetAttribLocation (s_prog, "aPos");
    s_attr_color = glGetAttribLocation (s_prog, "aCol");
    s_uni_mvp    = glGetUniformLocation(s_prog, "uMVP");
    s_uni_ptsz   = glGetUniformLocation(s_prog, "uPointSize");
    s_uni_ispt   = glGetUniformLocation(s_prog, "uIsPoint");

    glGenBuffers(1, &s_vbo_pos);
    glGenBuffers(1, &s_vbo_col);
}

// Build the combined MVP matrix (Projection * ModelView).
static void get_mvp(mat4 mvp) {
    mat4_multiply(mvp,
                  s_projection.stack[s_projection.top],
                  s_modelview.stack[s_modelview.top]);
}

// Convert quads (groups of 4 vertices) to triangles.
static std::vector<Vertex> quads_to_triangles(const std::vector<Vertex> &in) {
    std::vector<Vertex> out;
    for (size_t i = 0; i + 3 < in.size(); i += 4) {
        out.push_back(in[i]);
        out.push_back(in[i+1]);
        out.push_back(in[i+2]);
        out.push_back(in[i]);
        out.push_back(in[i+2]);
        out.push_back(in[i+3]);
    }
    return out;
}

// Flush vertex_buffer to GPU and draw.
// Static buffers avoid per-draw-call heap allocation.
static std::vector<Vertex> s_converted;
static std::vector<Vertex> s_thick_quads;
static std::vector<float>  s_pos;
static std::vector<float>  s_col;

// Emulate thick lines by expanding each segment to a screen-space quad.
// Called by flush_vertices when s_line_width > 1.0f and mode is a line type.
static void draw_thick_lines(const std::vector<Vertex>& verts, GLenum mode) {
    size_t n = verts.size();
    if (n < 2) return;

    mat4 mvp; get_mvp(mvp);

    float hw = (float)s_viewport[2] * 0.5f;
    float hh = (float)s_viewport[3] * 0.5f;
    if (hw < 1.0f || hh < 1.0f) return;

    float half_lw = s_line_width * 0.5f;

    // Project a vertex through MVP to NDC (perspective divide).
    auto project = [&](const Vertex& v, float& ox, float& oy) {
        float cx = mvp[0]*v.x + mvp[4]*v.y + mvp[8]*v.z  + mvp[12];
        float cy = mvp[1]*v.x + mvp[5]*v.y + mvp[9]*v.z  + mvp[13];
        float cw = mvp[3]*v.x + mvp[7]*v.y + mvp[11]*v.z + mvp[15];
        if (fabsf(cw) < 1e-6f) cw = 1.0f;
        ox = cx / cw;
        oy = cy / cw;
    };

    s_thick_quads.clear();

    // For GL_LINE_LOOP, draw n segments (last vertex back to first).
    size_t segs = (mode == GL_LINES)     ? n / 2 :
                  (mode == GL_LINE_LOOP) ? n     : n - 1;
    for (size_t si = 0; si < segs; si++) {
        const Vertex& va = (mode == GL_LINES)     ? verts[si*2]          :
                                                    verts[si];
        const Vertex& vb = (mode == GL_LINES)     ? verts[si*2+1]        :
                           (mode == GL_LINE_LOOP) ? verts[(si+1) % n]    :
                                                    verts[si+1];

        float ax, ay, bx, by;
        project(va, ax, ay);
        project(vb, bx, by);

        // Screen-space direction vector
        float dx = (bx - ax) * hw;
        float dy = (by - ay) * hh;
        float len = sqrtf(dx*dx + dy*dy);
        if (len < 0.5f) continue;

        // Perpendicular (rotated 90°), scaled to half-width in NDC
        float px = (-dy / len) * half_lw / hw;
        float py = ( dx / len) * half_lw / hh;

        // 4 corners of the quad (NDC, z=0)
        Vertex c[4];
        c[0] = va; c[0].x = ax - px; c[0].y = ay - py; c[0].z = 0.0f;
        c[1] = va; c[1].x = ax + px; c[1].y = ay + py; c[1].z = 0.0f;
        c[2] = vb; c[2].x = bx + px; c[2].y = by + py; c[2].z = 0.0f;
        c[3] = vb; c[3].x = bx - px; c[3].y = by - py; c[3].z = 0.0f;

        s_thick_quads.push_back(c[0]); s_thick_quads.push_back(c[1]); s_thick_quads.push_back(c[2]);
        s_thick_quads.push_back(c[0]); s_thick_quads.push_back(c[2]); s_thick_quads.push_back(c[3]);
    }

    if (s_thick_quads.empty()) return;

    size_t n2 = s_thick_quads.size();
    s_pos.resize(n2 * 3);
    s_col.resize(n2 * 4);
    for (size_t i = 0; i < n2; i++) {
        s_pos[i*3+0] = s_thick_quads[i].x;
        s_pos[i*3+1] = s_thick_quads[i].y;
        s_pos[i*3+2] = s_thick_quads[i].z;
        s_col[i*4+0] = s_thick_quads[i].r;
        s_col[i*4+1] = s_thick_quads[i].g;
        s_col[i*4+2] = s_thick_quads[i].b;
        s_col[i*4+3] = s_thick_quads[i].a;
    }

    glUseProgram(s_prog);

    // Draw quads in NDC using an identity MVP so vertices are passed through as-is.
    mat4 identity; mat4_identity(identity);
    glUniformMatrix4fv(s_uni_mvp, 1, GL_FALSE, identity);
    glUniform1f(s_uni_ptsz, 1.0f);
    glUniform1i(s_uni_ispt, 0);

    glBindBuffer(GL_ARRAY_BUFFER, s_vbo_pos);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n2*3*sizeof(float)), s_pos.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(s_attr_pos);
    glVertexAttribPointer(s_attr_pos, 3, GL_FLOAT, GL_FALSE, 0, 0);

    glBindBuffer(GL_ARRAY_BUFFER, s_vbo_col);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n2*4*sizeof(float)), s_col.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(s_attr_color);
    glVertexAttribPointer(s_attr_color, 4, GL_FLOAT, GL_FALSE, 0, 0);

    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)n2);

    glDisableVertexAttribArray(s_attr_pos);
    glDisableVertexAttribArray(s_attr_color);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void flush_vertices() {
    if (s_vbuf.empty()) return;

    std::vector<Vertex> *src = &s_vbuf;
    GLenum gl_mode = s_begin_mode;

    if (s_begin_mode == GL_POLYGON) {
        gl_mode = GL_TRIANGLE_FAN;
    } else if (s_begin_mode == GL_QUADS) {
        s_converted = quads_to_triangles(s_vbuf);
        src         = &s_converted;
        gl_mode     = GL_TRIANGLES;
    }

    if (src->empty()) return;

    // Thick line emulation: macOS WebGL (Metal) clamps glLineWidth to 1.
    // For any width > 1, expand line segments to screen-space quads instead.
    // GL_LINE_LOOP is also handled here — draw_thick_lines closes the loop.
    if (s_line_width > 1.05f && (gl_mode == GL_LINES || gl_mode == GL_LINE_STRIP ||
                                  gl_mode == GL_LINE_LOOP)) {
        draw_thick_lines(*src, gl_mode);
        return;
    }

    // Separate position and colour arrays for the VBOs.
    size_t n = src->size();
    s_pos.resize(n * 3);
    s_col.resize(n * 4);
    for (size_t i = 0; i < n; i++) {
        s_pos[i*3+0] = (*src)[i].x;
        s_pos[i*3+1] = (*src)[i].y;
        s_pos[i*3+2] = (*src)[i].z;
        s_col[i*4+0] = (*src)[i].r;
        s_col[i*4+1] = (*src)[i].g;
        s_col[i*4+2] = (*src)[i].b;
        s_col[i*4+3] = (*src)[i].a;
    }

    glUseProgram(s_prog);

    mat4 mvp; get_mvp(mvp);
    glUniformMatrix4fv(s_uni_mvp,  1, GL_FALSE, mvp);
    glUniform1f       (s_uni_ptsz, s_point_size);
    glUniform1i       (s_uni_ispt, s_begin_mode == GL_POINTS ? 1 : 0);

    // Position VBO
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo_pos);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n*3*sizeof(float)), s_pos.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(s_attr_pos);
    glVertexAttribPointer(s_attr_pos, 3, GL_FLOAT, GL_FALSE, 0, 0);

    // Colour VBO
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo_col);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n*4*sizeof(float)), s_col.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(s_attr_color);
    glVertexAttribPointer(s_attr_color, 4, GL_FLOAT, GL_FALSE, 0, 0);

    glDrawArrays(gl_mode, 0, (GLsizei)n);

    glDisableVertexAttribArray(s_attr_pos);
    glDisableVertexAttribArray(s_attr_color);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// ============================================================
// Public API
// ============================================================

void gles2_init() {
    init_shader();
    mat4_identity(s_modelview.stack[0]);
    mat4_identity(s_projection.stack[0]);
    s_modelview.top  = 0;
    s_projection.top = 0;
    s_matrix_mode    = GL_MODELVIEW;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void gles2_shutdown() {
    if (s_prog)    { glDeleteProgram(s_prog); s_prog = 0; }
    if (s_vbo_pos) { glDeleteBuffers(1, &s_vbo_pos); s_vbo_pos = 0; }
    if (s_vbo_col) { glDeleteBuffers(1, &s_vbo_col); s_vbo_col = 0; }
    s_lists.clear();
}

void gles2_get_mvp(float mvp[16]) {
    get_mvp(mvp);
}

// ---- Matrix stack ----

void glMatrixMode(GLenum mode) {
    if (s_is_recording) {
        DLCommand c; c.type = DLCommand::MATRIX_MODE; c.i[0] = (int)mode; dl_push(c); return;
    }
    s_matrix_mode = mode;
}

void glLoadIdentity() {
    if (s_is_recording) {
        DLCommand c; c.type = DLCommand::LOAD_IDENTITY; dl_push(c); return;
    }
    mat4_identity(current_matrix());
}

void glPushMatrix() {
    if (s_is_recording) {
        DLCommand c; c.type = DLCommand::PUSH_MATRIX; dl_push(c); return;
    }
    MatrixStack &st = current_stack();
    if (st.top < MAX_STACK_DEPTH - 1) {
        memcpy(st.stack[st.top+1], st.stack[st.top], sizeof(mat4));
        st.top++;
    }
}

void glPopMatrix() {
    if (s_is_recording) {
        DLCommand c; c.type = DLCommand::POP_MATRIX; dl_push(c); return;
    }
    MatrixStack &st = current_stack();
    if (st.top > 0) st.top--;
}

void glTranslatef(GLfloat x, GLfloat y, GLfloat z) {
    if (s_is_recording) {
        DLCommand c; c.type = DLCommand::TRANSLATEF;
        c.f[0] = x; c.f[1] = y; c.f[2] = z; dl_push(c); return;
    }
    mat4_translate_inplace(current_matrix(), x, y, z);
}

void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) {
    if (s_is_recording) {
        DLCommand c; c.type = DLCommand::ROTATEF;
        c.f[0] = angle; c.f[1] = x; c.f[2] = y; c.f[3] = z; dl_push(c); return;
    }
    mat4_rotate_inplace(current_matrix(), angle, x, y, z);
}

void glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z) {
    glRotatef((float)angle, (float)x, (float)y, (float)z);
}

void glScalef(GLfloat x, GLfloat y, GLfloat z) {
    if (s_is_recording) {
        DLCommand c; c.type = DLCommand::SCALEF;
        c.f[0] = x; c.f[1] = y; c.f[2] = z; dl_push(c); return;
    }
    mat4_scale_inplace(current_matrix(), x, y, z);
}

void glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t,
             GLdouble n, GLdouble f) {
    if (s_is_recording) {
        DLCommand c; c.type = DLCommand::ORTHO;
        c.f[0]=(float)l; c.f[1]=(float)r; c.f[2]=(float)b;
        c.f[3]=(float)t; c.f[4]=(float)n; c.f[5]=(float)f;
        dl_push(c); return;
    }
    mat4_ortho(current_matrix(), (float)l,(float)r,(float)b,(float)t,(float)n,(float)f);
}

void gluOrtho2D(GLdouble l, GLdouble r, GLdouble b, GLdouble t) {
    glOrtho(l, r, b, t, -1.0, 1.0);
}

void gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble near, GLdouble far) {
    if (s_is_recording) {
        DLCommand c; c.type = DLCommand::PERSPECTIVE;
        c.f[0]=(float)fovy; c.f[1]=(float)aspect;
        c.f[2]=(float)near; c.f[3]=(float)far;
        dl_push(c); return;
    }
    mat4_perspective(current_matrix(), (float)fovy, (float)aspect, (float)near, (float)far);
}

void gluLookAt(GLdouble ex, GLdouble ey, GLdouble ez,
               GLdouble cx, GLdouble cy, GLdouble cz,
               GLdouble ux, GLdouble uy, GLdouble uz) {
    if (s_is_recording) {
        DLCommand c; c.type = DLCommand::LOOKAT;
        c.f[0]=(float)ex; c.f[1]=(float)ey; c.f[2]=(float)ez;
        c.f[3]=(float)cx; c.f[4]=(float)cy; c.f[5]=(float)cz;
        c.f[6]=(float)ux; c.f[7]=(float)uy; c.f[8]=(float)uz;
        dl_push(c); return;
    }
    mat4_lookat(current_matrix(),
                (float)ex,(float)ey,(float)ez,
                (float)cx,(float)cy,(float)cz,
                (float)ux,(float)uy,(float)uz);
}

// ---- Immediate mode ----

void glBegin(GLenum mode) {
    if (s_is_recording) {
        DLCommand c; c.type = DLCommand::BEGIN; c.i[0] = (int)mode; dl_push(c); return;
    }
    s_begin_mode = mode;
    s_vbuf.clear();
    s_in_begin = true;
}

void glEnd() {
    if (s_is_recording) {
        DLCommand c; c.type = DLCommand::END; dl_push(c); return;
    }
    s_in_begin = false;
    flush_vertices();
    s_vbuf.clear();
}

void glVertex3f(GLfloat x, GLfloat y, GLfloat z) {
    if (s_is_recording) {
        DLCommand c; c.type = DLCommand::VERTEX3F;
        c.f[0] = x; c.f[1] = y; c.f[2] = z; dl_push(c); return;
    }
    Vertex v; v.x=x; v.y=y; v.z=z;
    v.r=s_color[0]; v.g=s_color[1]; v.b=s_color[2]; v.a=s_color[3];
    s_vbuf.push_back(v);
}

void glVertex2f(GLfloat x, GLfloat y) { glVertex3f(x, y, 0.0f); }
void glVertex2i(GLint   x, GLint   y) { glVertex3f((float)x, (float)y, 0.0f); }
void glVertex2fv(const GLfloat *v)    { glVertex3f(v[0], v[1], 0.0f); }

void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    if (s_is_recording) {
        DLCommand c; c.type = DLCommand::COLOR4F;
        c.f[0]=r; c.f[1]=g; c.f[2]=b; c.f[3]=a; dl_push(c); return;
    }
    s_color[0]=r; s_color[1]=g; s_color[2]=b; s_color[3]=a;
}
void glColor3f  (GLfloat r, GLfloat g, GLfloat b) { glColor4f(r, g, b, 1.0f); }
void glColor3fv (const GLfloat *v)                { glColor4f(v[0], v[1], v[2], 1.0f); }
void glColor4fv (const GLfloat *v)                { glColor4f(v[0], v[1], v[2], v[3]); }

// ---- Point size ----
void glPointSize(GLfloat size) {
    if (s_is_recording) {
        DLCommand c; c.type = DLCommand::POINT_SIZE; c.f[0] = size; dl_push(c); return;
    }
    s_point_size = size;
}

// ---- Line width (intercepted via macro in gles2_compat.h) ----
void gles2_set_line_width(GLfloat width) {
    if (s_is_recording) {
        DLCommand c; c.type = DLCommand::LINE_WIDTH; c.f[0] = width; dl_push(c); return;
    }
    s_line_width = width;
}

// ---- Viewport (intercepted via macro to keep s_viewport cache current) ----
void gles2_set_viewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    s_viewport[0] = x; s_viewport[1] = y;
    s_viewport[2] = (GLint)w; s_viewport[3] = (GLint)h;
    glViewport(x, y, w, h);  // macro not active here; calls native GL
}

// ---- Display lists ----

GLuint glGenLists(GLsizei range) {
    GLuint first = s_next_id;
    s_next_id += (GLuint)range;
    for (GLuint i = 0; i < (GLuint)range; i++)
        s_lists[first + i] = DisplayList();
    return first;
}

void glNewList(GLuint list, GLenum /*mode*/) {
    s_lists[list].cmds.clear();
    s_recording    = list;
    s_is_recording = true;
}

void glEndList() {
    s_is_recording = false;
    s_recording    = 0;
}

static void replay_list(GLuint id);  // forward declaration

void glCallList(GLuint id) {
    if (s_is_recording) {
        DLCommand c; c.type = DLCommand::CALL_LIST; c.i[0] = (int)id; dl_push(c); return;
    }
    replay_list(id);
}

void glDeleteLists(GLuint first, GLsizei range) {
    for (GLsizei i = 0; i < range; i++)
        s_lists.erase(first + (GLuint)i);
}

// Replay a display list by executing each captured command.
static void replay_list(GLuint id) {
    auto it = s_lists.find(id);
    if (it == s_lists.end()) return;
    for (const DLCommand &cmd : it->second.cmds) {
        switch (cmd.type) {
        case DLCommand::BEGIN:        glBegin      ((GLenum)cmd.i[0]); break;
        case DLCommand::END:          glEnd        (); break;
        case DLCommand::VERTEX3F:     glVertex3f   (cmd.f[0],cmd.f[1],cmd.f[2]); break;
        case DLCommand::COLOR4F:      glColor4f    (cmd.f[0],cmd.f[1],cmd.f[2],cmd.f[3]); break;
        case DLCommand::PUSH_MATRIX:  glPushMatrix (); break;
        case DLCommand::POP_MATRIX:   glPopMatrix  (); break;
        case DLCommand::LOAD_IDENTITY:glLoadIdentity(); break;
        case DLCommand::MATRIX_MODE:  glMatrixMode ((GLenum)cmd.i[0]); break;
        case DLCommand::TRANSLATEF:   glTranslatef (cmd.f[0],cmd.f[1],cmd.f[2]); break;
        case DLCommand::ROTATEF:      glRotatef    (cmd.f[0],cmd.f[1],cmd.f[2],cmd.f[3]); break;
        case DLCommand::SCALEF:       glScalef     (cmd.f[0],cmd.f[1],cmd.f[2]); break;
        case DLCommand::ORTHO:        glOrtho      (cmd.f[0],cmd.f[1],cmd.f[2],
                                                    cmd.f[3],cmd.f[4],cmd.f[5]); break;
        case DLCommand::PERSPECTIVE:  gluPerspective(cmd.f[0],cmd.f[1],cmd.f[2],cmd.f[3]); break;
        case DLCommand::LOOKAT:       gluLookAt    (cmd.f[0],cmd.f[1],cmd.f[2],
                                                    cmd.f[3],cmd.f[4],cmd.f[5],
                                                    cmd.f[6],cmd.f[7],cmd.f[8]); break;
        case DLCommand::POINT_SIZE:   glPointSize  (cmd.f[0]); break;
        case DLCommand::LINE_WIDTH:   glLineWidth  (cmd.f[0]); break;
        case DLCommand::CALL_LIST:    glCallList   ((GLuint)cmd.i[0]); break;
        case DLCommand::CLEAR:        glClear      ((GLbitfield)cmd.i[0]); break;
        case DLCommand::CLEAR_COLOR:  glClearColor (cmd.f[0],cmd.f[1],cmd.f[2],cmd.f[3]); break;
        case DLCommand::VIEWPORT:     glViewport   (cmd.i[0],cmd.i[1],
                                                    (GLsizei)cmd.i[2],(GLsizei)cmd.i[3]); break;
        }
    }
}

// ---- GLUT timing stub ----
int glutGet(GLenum query) {
    if (query == GLUT_ELAPSED_TIME)
        return (int)SDL_GetTicks();
    return 0;
}
