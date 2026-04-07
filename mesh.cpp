// Static GPU geometry — see mesh.h for documentation.

// Desktop GL loading must happen before any GL header.
#if !defined(__ANDROID__) && !defined(__IOS__) && !defined(__EMSCRIPTEN__)
#  if defined(__linux__)
#    ifndef GL_GLEXT_PROTOTYPES
#      define GL_GLEXT_PROTOTYPES
#    endif
#    include <GL/gl.h>
#    include <GL/glext.h>
#  elif defined(__APPLE__)
#    include <OpenGL/gl3.h>
#  elif defined(_WIN32)
#    include <windows.h>
#    undef near
#    undef far
#    include <GL/gl.h>
#    include <GL/glext.h>
#  endif
#endif

#include "mesh.h"
#include "gles2_compat.h"
#include "mat4.h"

// Undefine the compat_ redirect macros so we can call real GL functions
// directly in this translation unit.
#ifdef DESKTOP_COMPAT_GL
#  undef glLineWidth
#  undef glViewport
#  undef glMatrixMode
#  undef glLoadIdentity
#  undef glPushMatrix
#  undef glPopMatrix
#  undef glTranslatef
#  undef glRotatef
#  undef glRotated
#  undef glScalef
#  undef glOrtho
#  undef gluOrtho2D
#  undef gluPerspective
#  undef gluLookAt
#  undef glBegin
#  undef glEnd
#  undef glVertex2f
#  undef glVertex2i
#  undef glVertex2fv
#  undef glVertex3f
#  undef glColor3f
#  undef glColor4f
#  undef glColor3fv
#  undef glColor4fv
#  undef glPointSize
#  undef glGenLists
#  undef glNewList
#  undef glEndList
#  undef glCallList
#  undef glDeleteLists
#  undef glAccum
#  undef glClearAccum
#endif

// ============================================================
// MeshBuilder
// ============================================================

MeshBuilder::MeshBuilder()
    : cur_r_(1), cur_g_(1), cur_b_(1), cur_a_(1),
      cur_mode_(GL_TRIANGLES), in_group_(false), group_start_(0) {}

void MeshBuilder::begin(GLenum mode) {
    cur_mode_    = mode;
    in_group_    = true;
    group_start_ = (int)(pos_.size() / 3);
}

void MeshBuilder::color(float r, float g, float b, float a) {
    cur_r_ = r; cur_g_ = g; cur_b_ = b; cur_a_ = a;
}

void MeshBuilder::vertex(float x, float y, float z) {
    pos_.push_back(x); pos_.push_back(y); pos_.push_back(z);
    col_.push_back(cur_r_); col_.push_back(cur_g_);
    col_.push_back(cur_b_); col_.push_back(cur_a_);
}

void MeshBuilder::end() {
    if (!in_group_) return;
    int count = (int)(pos_.size() / 3) - group_start_;
    if (count > 0) {
        MeshGroup g;
        g.mode         = cur_mode_;
        g.vertex_start = group_start_;
        g.vertex_count = count;
        groups_.push_back(g);
    }
    in_group_ = false;
}

void MeshBuilder::clear() {
    groups_.clear();
    pos_.clear();
    col_.clear();
    in_group_ = false;
}

// ============================================================
// Mesh
// ============================================================

Mesh::Mesh(Mesh&& o) noexcept
    : vbo_pos_(o.vbo_pos_), vbo_col_(o.vbo_col_),
#ifdef DESKTOP_COMPAT_GL
      vao_(o.vao_),
#endif
      groups_(std::move(o.groups_)), vertex_count_(o.vertex_count_) {
    o.vbo_pos_ = 0; o.vbo_col_ = 0;
#ifdef DESKTOP_COMPAT_GL
    o.vao_ = 0;
#endif
    o.vertex_count_ = 0;
}

Mesh::Mesh() : vbo_pos_(0), vbo_col_(0),
#ifdef DESKTOP_COMPAT_GL
               vao_(0),
#endif
               vertex_count_(0) {
    glGenBuffers(1, &vbo_pos_);
    glGenBuffers(1, &vbo_col_);
#ifdef DESKTOP_COMPAT_GL
    glGenVertexArrays(1, &vao_);
#endif
}

Mesh::~Mesh() {
    if (vbo_pos_) glDeleteBuffers(1, &vbo_pos_);
    if (vbo_col_) glDeleteBuffers(1, &vbo_col_);
#ifdef DESKTOP_COMPAT_GL
    if (vao_)     glDeleteVertexArrays(1, &vao_);
#endif
}

void Mesh::upload(const MeshBuilder& mb, GLenum usage) {
    groups_       = mb.groups();
    vertex_count_ = mb.vertex_count();
    if (vertex_count_ == 0) return;

    const GLCompatProg* p = gles2_program_info();

#ifdef DESKTOP_COMPAT_GL
    glBindVertexArray(vao_);
#endif

    glBindBuffer(GL_ARRAY_BUFFER, vbo_pos_);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(mb.positions().size() * sizeof(float)),
                 mb.positions().data(), usage);
    glEnableVertexAttribArray(p->attr_pos);
    glVertexAttribPointer(p->attr_pos, 3, GL_FLOAT, GL_FALSE, 0, 0);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_col_);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(mb.colours().size() * sizeof(float)),
                 mb.colours().data(), usage);
    glEnableVertexAttribArray(p->attr_col);
    glVertexAttribPointer(p->attr_col, 4, GL_FLOAT, GL_FALSE, 0, 0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
#ifdef DESKTOP_COMPAT_GL
    glBindVertexArray(0);
#endif
}

void Mesh::draw_with_mvp(const float mvp[16], float point_size) const {
    if (groups_.empty()) return;

    const GLCompatProg* p = gles2_program_info();

    glUseProgram(p->prog);
    glUniformMatrix4fv(p->uni_mvp,  1, GL_FALSE, mvp);
    glUniform1f       (p->uni_ptsz, point_size);

#ifdef DESKTOP_COMPAT_GL
    glBindVertexArray(vao_);
#else
    // GLES2/WebGL: re-bind attribute pointers per group using a byte offset so
    // that glDrawArrays is always called with first=0.  Safari's WebGL/Metal
    // backend has a bug where glDrawArrays with a non-zero first parameter does
    // not correctly advance into the VBO, causing every group beyond the first
    // (vertex_start > 0) to render garbage or nothing.
    glEnableVertexAttribArray(p->attr_pos);
    glEnableVertexAttribArray(p->attr_col);
#endif

    for (const MeshGroup& g : groups_) {
        glUniform1i(p->uni_ispt, g.mode == GL_POINTS ? 1 : 0);
#ifdef DESKTOP_COMPAT_GL
        glDrawArrays(g.mode, g.vertex_start, g.vertex_count);
#else
        const uintptr_t pos_off = (uintptr_t)(g.vertex_start) * 3 * sizeof(float);
        const uintptr_t col_off = (uintptr_t)(g.vertex_start) * 4 * sizeof(float);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_pos_);
        glVertexAttribPointer(p->attr_pos, 3, GL_FLOAT, GL_FALSE, 0, (const void*)pos_off);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_col_);
        glVertexAttribPointer(p->attr_col, 4, GL_FLOAT, GL_FALSE, 0, (const void*)col_off);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glDrawArrays(g.mode, 0, g.vertex_count);
#endif
    }

#ifndef DESKTOP_COMPAT_GL
    glDisableVertexAttribArray(p->attr_pos);
    glDisableVertexAttribArray(p->attr_col);
#endif
}

void Mesh::draw_internal(float point_size) const {
    float mvp[16];
    gles2_get_mvp(mvp);
    draw_with_mvp(mvp, point_size);
}

void Mesh::draw(float point_size) const {
    draw_internal(point_size);
}

void Mesh::draw_tinted(float r, float g, float b, float a,
                       float point_size) const {
    gles2_set_tint(r, g, b, a);
    draw_internal(point_size);
    gles2_set_tint(1.0f, 1.0f, 1.0f, 1.0f);
}

void Mesh::draw_at(float x, float y, float angle_deg, float point_size) const {
    float vp[16];
    gles2_get_mvp(vp);
    float rad = angle_deg * (float)M_PI / 180.0f;
    float c = cosf(rad), s = sinf(rad);
    // Column-major T*R model matrix (rotate then translate to world position)
    float model[16] = {
         c,  s, 0, 0,
        -s,  c, 0, 0,
         0,  0, 1, 0,
         x,  y, 0, 1
    };
    float mvp[16];
    mat4_mul(mvp, vp, model);
    draw_with_mvp(mvp, point_size);
}

void Mesh::draw_tinted_at(float r, float g, float b, float a,
                          float x, float y, float angle_deg,
                          float point_size) const {
    gles2_set_tint(r, g, b, a);
    draw_at(x, y, angle_deg, point_size);
    gles2_set_tint(1.0f, 1.0f, 1.0f, 1.0f);
}

void Mesh::draw_with_model(const float model[16], float point_size) const {
    float vp[16], mvp[16];
    gles2_get_mvp(vp);
    mat4_mul(mvp, vp, model);
    draw_with_mvp(mvp, point_size);
}

void Mesh::draw_tinted_with_model(float r, float g, float b, float a,
                                  const float model[16], float point_size) const {
    gles2_set_tint(r, g, b, a);
    draw_with_model(model, point_size);
    gles2_set_tint(1.0f, 1.0f, 1.0f, 1.0f);
}
