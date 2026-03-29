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
//   Desktop  – OpenGL 2.0 GLSL + glGetFloatv for the MVP
//   GLES2    – same shader dialect; gles2_get_mvp() for the MVP

#include "gl_compat.h"
#include "asteroid.h"

class WarpPass {
public:
    WarpPass();
    ~WarpPass();

    // Capture the current viewport into the internal scene texture.
    // Call after the main scene (stars + game objects) is fully rendered
    // but before the warp polygons are drawn.
    void capture(int vp_x, int vp_y, int vp_w, int vp_h);

    // Draw the warp distortion polygon for one invisible asteroid.
    //   ax, ay  – asteroid world-space position in the same coordinate frame
    //             that was active when the asteroid was drawn (i.e. tile-
    //             translated camera-relative coords).
    // The caller must have the same MVP matrices active as in the main pass.
    void draw(const Asteroid *a, float ax, float ay,
              int vp_x, int vp_y, int vp_w, int vp_h);

private:
    void init_shader();

    GLuint tex_;        // scene snapshot texture
    GLuint prog_;       // warp GLSL program

    int tex_w_, tex_h_; // dimensions of the last captured texture

    // Shader locations
    int a_pos_;
    int u_mvp_;
    int u_tex_;
    int u_center_ndc_;
    int u_radius_ndc_;
    int u_time_;
};
