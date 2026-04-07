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
#include <cmath>

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
#ifndef DESKTOP_COMPAT_GL
        g.vbo_pos = 0;
        g.vbo_col = 0;
#endif
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

void MeshBuilder::flatten_to_lines() {
    if (in_group_) end();
    if (groups_.size() <= 1) return; // Already single-group — nothing to do.

    // Only flatten if every group is a line primitive.  If any group uses a
    // different mode (GL_POINTS, GL_TRIANGLES, …) bail out unchanged.
    for (const MeshGroup& g : groups_) {
        if (g.mode != GL_LINES && g.mode != GL_LINE_STRIP && g.mode != GL_LINE_LOOP)
            return;
    }

    std::vector<float> new_pos, new_col;

    for (const MeshGroup& g : groups_) {
        const float* gp = pos_.data() + (size_t)g.vertex_start * 3;
        const float* gc = col_.data() + (size_t)g.vertex_start * 4;
        int n = g.vertex_count;

        if (g.mode == GL_LINES) {
            // Vertices are already in [start, end] pairs — copy verbatim.
            new_pos.insert(new_pos.end(), gp,        gp + (size_t)n * 3);
            new_col.insert(new_col.end(), gc,        gc + (size_t)n * 4);
        } else if (g.mode == GL_LINE_STRIP) {
            // n vertices → n-1 segments, each emitted as two GL_LINES vertices.
            for (int i = 0; i < n - 1; ++i) {
                new_pos.insert(new_pos.end(), gp + i*3,     gp + i*3 + 3);
                new_col.insert(new_col.end(), gc + i*4,     gc + i*4 + 4);
                new_pos.insert(new_pos.end(), gp + (i+1)*3, gp + (i+1)*3 + 3);
                new_col.insert(new_col.end(), gc + (i+1)*4, gc + (i+1)*4 + 4);
            }
        } else { // GL_LINE_LOOP
            // n vertices → n segments (last vertex connects back to first).
            for (int i = 0; i < n; ++i) {
                int j = (i + 1) % n;
                new_pos.insert(new_pos.end(), gp + i*3, gp + i*3 + 3);
                new_col.insert(new_col.end(), gc + i*4, gc + i*4 + 4);
                new_pos.insert(new_pos.end(), gp + j*3, gp + j*3 + 3);
                new_col.insert(new_col.end(), gc + j*4, gc + j*4 + 4);
            }
        }
    }

    // Replace the builder's data with the flattened GL_LINES content.
    pos_ = std::move(new_pos);
    col_ = std::move(new_col);
    groups_.clear();

    if (!pos_.empty()) {
        MeshGroup g;
        g.mode         = GL_LINES;
        g.vertex_start = 0;
        g.vertex_count = (int)(pos_.size() / 3);
#ifndef DESKTOP_COMPAT_GL
        g.vbo_pos = 0;
        g.vbo_col = 0;
#endif
        groups_.push_back(g);
    }
}

void MeshBuilder::thicken_lines(float half_width) {
    if (in_group_) end();
    if (groups_.empty()) return;

    bool has_lines = false;
    for (const MeshGroup& g : groups_)
        if (g.mode == GL_LINES) { has_lines = true; break; }
    if (!has_lines) return;

    std::vector<MeshGroup> new_groups;
    std::vector<float>     new_pos, new_col;

    for (const MeshGroup& g : groups_) {
        if (g.mode != GL_LINES) {
            // Pass non-line groups through unchanged, fixing up vertex_start.
            MeshGroup ng = g;
            ng.vertex_start = (int)(new_pos.size() / 3);
            new_groups.push_back(ng);
            new_pos.insert(new_pos.end(),
                           pos_.begin() + (size_t)g.vertex_start * 3,
                           pos_.begin() + (size_t)g.vertex_start * 3 + (size_t)g.vertex_count * 3);
            new_col.insert(new_col.end(),
                           col_.begin() + (size_t)g.vertex_start * 4,
                           col_.begin() + (size_t)g.vertex_start * 4 + (size_t)g.vertex_count * 4);
            continue;
        }

        MeshGroup ng;
        ng.mode         = GL_TRIANGLES;
        ng.vertex_start = (int)(new_pos.size() / 3);
        ng.vertex_count = 0;
#ifndef DESKTOP_COMPAT_GL
        ng.vbo_pos = 0;
        ng.vbo_col = 0;
#endif
        const float* gp = pos_.data() + (size_t)g.vertex_start * 3;
        const float* gc = col_.data() + (size_t)g.vertex_start * 4;

        // Each GL_LINES segment is a consecutive (a, b) vertex pair.
        for (int i = 0; i + 1 < g.vertex_count; i += 2) {
            float ax = gp[i*3+0], ay = gp[i*3+1], az = gp[i*3+2];
            float bx = gp[i*3+3], by = gp[i*3+4], bz = gp[i*3+5];

            float dx = bx - ax, dy = by - ay;
            float len = sqrtf(dx*dx + dy*dy);
            if (len < 1e-6f) continue;

            // Perpendicular unit vector scaled to half_width.
            float px = -dy / len * half_width;
            float py =  dx / len * half_width;

            // Four quad corners (winding: CCW).
            float qx[4] = { ax - px, ax + px, bx + px, bx - px };
            float qy[4] = { ay - py, ay + py, by + py, by - py };
            float qz[4] = { az,      az,      bz,      bz      };

            const float* ca = gc + i * 4;
            const float* cb = gc + (i + 1) * 4;

            // Two triangles: (0,1,2) and (0,2,3).
            const int tri[6] = { 0, 1, 2, 0, 2, 3 };
            for (int t = 0; t < 6; t++) {
                int vi = tri[t];
                new_pos.push_back(qx[vi]);
                new_pos.push_back(qy[vi]);
                new_pos.push_back(qz[vi]);
                const float* c = (vi < 2) ? ca : cb;
                new_col.push_back(c[0]);
                new_col.push_back(c[1]);
                new_col.push_back(c[2]);
                new_col.push_back(c[3]);
                ng.vertex_count++;
            }
        }

        if (ng.vertex_count > 0)
            new_groups.push_back(ng);
    }

    groups_ = std::move(new_groups);
    pos_    = std::move(new_pos);
    col_    = std::move(new_col);
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
    // groups_ was moved into this; the source now has an empty vector, which is
    // correct — no per-group VBO IDs remain in o to double-delete.
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
#else
    for (MeshGroup& g : groups_) {
        if (g.vbo_pos) glDeleteBuffers(1, &g.vbo_pos);
        if (g.vbo_col) glDeleteBuffers(1, &g.vbo_col);
    }
#endif
}

void Mesh::upload(const MeshBuilder& mb, GLenum usage) {
    // Delete any existing per-group VBOs before replacing geometry.
#ifndef DESKTOP_COMPAT_GL
    for (MeshGroup& g : groups_) {
        if (g.vbo_pos) glDeleteBuffers(1, &g.vbo_pos);
        if (g.vbo_col) glDeleteBuffers(1, &g.vbo_col);
    }
#endif

    groups_       = mb.groups();
    vertex_count_ = mb.vertex_count();

    // Retain a CPU copy whenever any group is a line primitive so that
    // draw_with_mvp() can feed gles2_draw_thick_lines_mvp() at draw time.
    bool has_lines = false;
    for (const MeshGroup& g : groups_)
        if (g.mode == GL_LINES || g.mode == GL_LINE_STRIP || g.mode == GL_LINE_LOOP)
            { has_lines = true; break; }
    if (has_lines) {
        cpu_pos_.assign(mb.positions().begin(), mb.positions().end());
        cpu_col_.assign(mb.colours().begin(),   mb.colours().end());
    } else {
        cpu_pos_.clear();
        cpu_col_.clear();
    }

    if (vertex_count_ == 0) return;

#ifdef DESKTOP_COMPAT_GL
    // Desktop: upload all vertices into two shared VBOs and record attribute
    // state in the VAO.
    const GLCompatProg* p = gles2_program_info();
    glBindVertexArray(vao_);

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
    glBindVertexArray(0);
#else
    // GLES2/WebGL: give each group its own VBO pair so that glDrawArrays is
    // always called with first=0 and glVertexAttribPointer always with offset 0.
    // This avoids both the Safari/Metal non-zero-first bug and Emscripten's
    // JS-side WebGL attribute-state cache becoming stale when many meshes are
    // uploaded in sequence (e.g. Typer::init_meshes building ~60 char meshes).
    for (MeshGroup& g : groups_) {
        const float* pos_data = mb.positions().data() + (size_t)g.vertex_start * 3;
        const float* col_data = mb.colours().data()   + (size_t)g.vertex_start * 4;

        glGenBuffers(1, &g.vbo_pos);
        glBindBuffer(GL_ARRAY_BUFFER, g.vbo_pos);
        glBufferData(GL_ARRAY_BUFFER,
                     (GLsizeiptr)((size_t)g.vertex_count * 3 * sizeof(float)),
                     pos_data, usage);

        glGenBuffers(1, &g.vbo_col);
        glBindBuffer(GL_ARRAY_BUFFER, g.vbo_col);
        glBufferData(GL_ARRAY_BUFFER,
                     (GLsizeiptr)((size_t)g.vertex_count * 4 * sizeof(float)),
                     col_data, usage);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
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
    glEnableVertexAttribArray(p->attr_pos);
    glEnableVertexAttribArray(p->attr_col);
#endif

    for (const MeshGroup& g : groups_) {
        glUniform1i(p->uni_ispt, g.mode == GL_POINTS ? 1 : 0);
#ifdef DESKTOP_COMPAT_GL
        glDrawArrays(g.mode, g.vertex_start, g.vertex_count);
#else
        // On GLES2/WebGL glLineWidth > 1 is ignored; use the screen-space quad
        // emulation so line-mode meshes get the same thick rendering as the
        // immediate-mode glBegin/glEnd path did before the VBO migration.
        bool is_line = (g.mode == GL_LINES || g.mode == GL_LINE_STRIP ||
                        g.mode == GL_LINE_LOOP);
        if (is_line && !cpu_pos_.empty() && gles2_get_line_width() > 1.05f) {
            gles2_draw_thick_lines_mvp(
                cpu_pos_.data() + (size_t)g.vertex_start * 3,
                cpu_col_.data() + (size_t)g.vertex_start * 4,
                g.vertex_count, g.mode, mvp);
            continue;
        }
        // Bind each group's own VBO at offset 0.  This avoids both the
        // Safari/Metal non-zero first= bug and stale Emscripten attribute-cache
        // entries caused by uploading many meshes with shared VBOs.
        glBindBuffer(GL_ARRAY_BUFFER, g.vbo_pos);
        glVertexAttribPointer(p->attr_pos, 3, GL_FLOAT, GL_FALSE, 0, 0);
        glBindBuffer(GL_ARRAY_BUFFER, g.vbo_col);
        glVertexAttribPointer(p->attr_col, 4, GL_FLOAT, GL_FALSE, 0, 0);
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
