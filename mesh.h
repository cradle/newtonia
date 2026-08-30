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

    // The ember pattern (see CLAUDE.md "Particles"): point-like effects are
    // tiny GL_TRIANGLES diamonds, never GL_POINTS — a field GPU silently
    // drops whole GL_POINTS draws (debris/trails 2026-08-23, the starfield
    // 2026-08-30), so nothing player-visible may go through one. Call these
    // between begin(GL_TRIANGLES) and end(); both emit six vertices in the
    // current colour.
    //
    // ember(): a diamond streaked along its velocity so debris still reads
    // as a spark — half-length len along v, half-width wid across it (a
    // zero velocity streaks along +x).
    // dot(): the axis-aligned special case for markers with nothing to
    // streak along (stars, minimap dots, turret hubs), half-extent r.
    void ember(float x, float y, float vx, float vy, float len, float wid);
    void dot(float x, float y, float r, float z = 0.0f);

    // Access collected data.
    const std::vector<MeshGroup>& groups()   const { return groups_; }
    const std::vector<float>&     positions() const { return pos_; }
    const std::vector<float>&     colours()   const { return col_; }
    int                            vertex_count() const { return (int)(pos_.size() / 3); }

    void clear();

    // Append another builder's collected groups with a position offset —
    // used to assemble one mesh from prebuilt pieces (e.g. a text string
    // from per-glyph builders). Must not be called mid-group.
    void append_translated(const MeshBuilder& src, float dx, float dy = 0.0f);

    // append_translated's bigger sibling: apply a full column-major 4x4
    // model transform to every vertex and multiply an RGBA tint into every
    // colour. Lets many instances of a small builder be baked into one
    // world-space batch (AsteroidDrawer's dead-asteroid score labels).
    void append_transformed(const MeshBuilder& src, const float model[16],
                            float r, float g, float b, float a);

    // Merge all GL_LINE_STRIP / GL_LINE_LOOP / GL_LINES groups into a single
    // GL_LINES group.  This eliminates multi-group meshes so that draw_with_mvp()
    // only ever needs one glVertexAttribPointer + glDrawArrays sequence.
    // Groups using other primitive modes (e.g. GL_POINTS) are left unchanged;
    // if any such groups exist the function returns without modifying anything.
    void flatten_to_lines();

    // Expand every GL_LINES group into a GL_TRIANGLES group of screen-aligned
    // quads.  Each line segment (vertex pair) becomes two triangles whose
    // perpendicular half-width is half_width in local coordinates.  Call after
    // flatten_to_lines() so the input is a single GL_LINES group.  Non-line
    // groups are left unchanged.  This pre-bakes thick-line rendering into the
    // mesh geometry so that WebGL (which ignores glLineWidth > 1) and other
    // GLES2 platforms produce visibly-thick strokes without per-frame CPU work.
    void thicken_lines(float half_width);

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

    // CPU-side copy of vertex data retained for line-mode groups so that
    // draw_with_mvp() can call gles2_draw_thick_lines_mvp() at draw time.
    // Only populated when at least one group uses a line primitive mode.
    std::vector<float> cpu_pos_;  // vertex_count * 3 floats (parallel to GPU data)
    std::vector<float> cpu_col_;  // vertex_count * 4 floats
};
