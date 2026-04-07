#pragma once
// Warp / gravitational-lens distortion pass for invisible asteroids.
//
// After the main scene (stars + game objects) has been drawn, WarpPass
// captures the current viewport into a texture, then redraws each invisible
// asteroid's polygon with a radial-distortion shader.  The shader samples the
// captured texture at positions displaced outward from the asteroid centre,
// pulling exterior scene content into the asteroid area and creating a
// visible gravitational-lens effect.
//
// Works on all targets:
//   Desktop  – OpenGL 3.3 Core GLSL 1.50; gles2_get_mvp() for the MVP
//   GLES2    – GLSL ES 1.00;              gles2_get_mvp() for the MVP

#include "gl_compat.h"
#include "asteroid.h"

class WarpPass {
public:
    WarpPass();
    ~WarpPass();

    // Capture the current viewport into the internal scene texture.
    void capture(int vp_x, int vp_y, int vp_w, int vp_h);

    // Draw the warp distortion polygon for one invisible asteroid.
    void draw(const Asteroid *a, float ax, float ay,
              int vp_x, int vp_y, int vp_w, int vp_h);

private:
    void init_shader();

    GLuint tex_;        // scene snapshot texture
    GLuint prog_;       // warp GLSL program
    GLuint vbo_;        // vertex buffer for the asteroid polygon
#ifdef DESKTOP_COMPAT_GL
    GLuint vao_;        // vertex array object (required in GL 3.3 Core)
#endif

    int tex_w_, tex_h_; // dimensions of the last captured texture

    // Shader locations
    int a_pos_;
    int u_mvp_;
    int u_tex_;
    int u_center_ndc_;
    int u_radius_ndc_;
    int u_time_;
};
