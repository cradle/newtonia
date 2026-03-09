// OpenGL ES 2.0 compatibility shim implementation.
// Provides a subset of the OpenGL 1.x fixed-function pipeline using GLES2
// shaders + vertex arrays.

#include "gles2_compat.h"

#include <SDL.h>
#include <GLES2/gl2.h>

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
    "void main(){ gl_FragColor = vCol; }\n";

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
static void flush_vertices() {
    if (s_vbuf.empty()) return;

    std::vector<Vertex> *src = &s_vbuf;
    std::vector<Vertex> converted;
    GLenum gl_mode = s_begin_mode;

    if (s_begin_mode == GL_POLYGON) {
        gl_mode = GL_TRIANGLE_FAN;
    } else if (s_begin_mode == GL_QUADS) {
        converted = quads_to_triangles(s_vbuf);
        src       = &converted;
        gl_mode   = GL_TRIANGLES;
    }

    if (src->empty()) return;

    // Separate position and colour arrays for the VBOs.
    size_t n = src->size();
    std::vector<float> pos(n * 3), col(n * 4);
    for (size_t i = 0; i < n; i++) {
        pos[i*3+0] = (*src)[i].x;
        pos[i*3+1] = (*src)[i].y;
        pos[i*3+2] = (*src)[i].z;
        col[i*4+0] = (*src)[i].r;
        col[i*4+1] = (*src)[i].g;
        col[i*4+2] = (*src)[i].b;
        col[i*4+3] = (*src)[i].a;
    }

    glUseProgram(s_prog);

    mat4 mvp; get_mvp(mvp);
    glUniformMatrix4fv(s_uni_mvp,  1, GL_FALSE, mvp);
    glUniform1f       (s_uni_ptsz, s_point_size);

    // Position VBO
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo_pos);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n*3*sizeof(float)), pos.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(s_attr_pos);
    glVertexAttribPointer(s_attr_pos, 3, GL_FLOAT, GL_FALSE, 0, 0);

    // Colour VBO
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo_col);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n*4*sizeof(float)), col.data(), GL_STREAM_DRAW);
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
