#pragma once
// Static GPU geometry helper.
//
// Mesh holds a pre-built VBO (positions + colours) that is drawn once per
// frame with the current matrix-stack MVP from gles2_get_mvp().  It uses the
// same GLSL program as the gles2_compat shim so there is no additional shader
// compilation.
//
// Usage:
//   Mesh m;
//   MeshBuilder mb;
//   mb.begin(GL_LINE_LOOP);
//   mb.color(1,0,0,1); mb.vertex(0,1); mb.vertex(-1,-1); mb.vertex(1,-1);
//   mb.end();
//   m.upload(mb);        // uploads once to GPU
//   ...
//   m.draw();            // each frame; uses current matrix stack
//   m.draw_tinted(r,g,b,a);  // same but with a colour tint

#include "gl_compat.h"
#include <vector>

// ---- MeshBuilder -------------------------------------------------------
// Lightweight immediate-mode-style collector that assembles interleaved
// position + colour data for one or more primitive groups.

struct MeshGroup {
    GLenum mode;
    int    vertex_start;   // byte offset into shared VBO (desktop/VAO path only)
    int    vertex_count;
#ifndef DESKTOP_COMPAT_GL
    // GLES2/WebGL: each group owns its own VBO pair so drawing always uses
    // offset 0 and glDrawArrays first=0, avoiding Metal/ANGLE driver bugs with
    // non-zero offsets.
    GLuint vbo_pos;
    GLuint vbo_col;
#endif
};

class MeshBuilder {
public:
    MeshBuilder();

    void begin(GLenum mode);
    void color(float r, float g, float b, float a = 1.0f);
    void vertex(float x, float y, float z = 0.0f);
    void end();

    // Access collected data.
    const std::vector<MeshGroup>& groups()   const { return groups_; }
    const std::vector<float>&     positions() const { return pos_; }
    const std::vector<float>&     colours()   const { return col_; }
    int                            vertex_count() const { return (int)(pos_.size() / 3); }

    void clear();

private:
    std::vector<MeshGroup> groups_;
    std::vector<float>     pos_;  // 3 floats per vertex
    std::vector<float>     col_;  // 4 floats per vertex
    float cur_r_, cur_g_, cur_b_, cur_a_;
    GLenum cur_mode_;
    bool   in_group_;
    int    group_start_;
};

// ---- Mesh --------------------------------------------------------------

class Mesh {
public:
    Mesh();
    ~Mesh();

    // Non-copyable, movable.
    Mesh(const Mesh&)            = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& o) noexcept;
    Mesh& operator=(Mesh&&)      = delete;

    // Upload geometry from a MeshBuilder.  May be called multiple times to
    // replace previous geometry.  Pass GL_DYNAMIC_DRAW or GL_STREAM_DRAW for
    // geometry that changes every frame.
    void upload(const MeshBuilder& mb, GLenum usage = GL_STATIC_DRAW);

    // Draw with current matrix-stack MVP and no tint (1,1,1,1).
    void draw(float point_size = 1.0f) const;

    // Draw with an RGBA tint applied in the fragment shader.
    void draw_tinted(float r, float g, float b, float a,
                     float point_size = 1.0f) const;

    // Draw at world position (x,y) rotated by angle_deg around Z, without
    // touching the shim matrix stack.  The current VP from gles2_get_mvp() is
    // combined with the model transform using plain C math.
    void draw_at(float x, float y, float angle_deg,
                 float point_size = 1.0f) const;
    void draw_tinted_at(float r, float g, float b, float a,
                        float x, float y, float angle_deg,
                        float point_size = 1.0f) const;

    // Draw with an explicit 4x4 column-major model matrix combined with the
    // current VP from gles2_get_mvp().
    void draw_with_model(const float model[16], float point_size = 1.0f) const;
    void draw_tinted_with_model(float r, float g, float b, float a,
                                const float model[16],
                                float point_size = 1.0f) const;

    bool empty() const { return groups_.empty(); }

private:
    void draw_internal(float point_size) const;
    void draw_with_mvp(const float mvp[16], float point_size) const;

    GLuint vbo_pos_;
    GLuint vbo_col_;
#ifdef DESKTOP_COMPAT_GL
    GLuint vao_;
#endif
    std::vector<MeshGroup> groups_;
    int vertex_count_;
};
