#pragma once
// Minimal 4x4 column-major matrix math for the renderer.
// Replaces the gles2_compat shim matrix stack for callers that compute
// their own transforms.
//
// Storage: column-major, 16 floats.  Entry at (row r, col c) is m[c*4+r].
// This matches OpenGL's glUniformMatrix4fv layout.

#include <math.h>
#include <string.h>

static inline void mat4_identity(float m[16]) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

// out = a * b  (may alias: uses a temporary)
static inline void mat4_mul(float out[16], const float a[16], const float b[16]) {
    float tmp[16];
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a[r + k*4] * b[k + c*4];
            tmp[r + c*4] = s;
        }
    memcpy(out, tmp, 16 * sizeof(float));
}

// out = m * translate(tx,ty,tz)
static inline void mat4_translate(float out[16], const float m[16],
                                   float tx, float ty, float tz) {
    float t[16]; mat4_identity(t);
    t[12] = tx; t[13] = ty; t[14] = tz;
    mat4_mul(out, m, t);
}

// out = m * rotate_z(angle_deg)
static inline void mat4_rotate_z(float out[16], const float m[16], float angle_deg) {
    float r[16]; mat4_identity(r);
    float rad = angle_deg * (float)M_PI / 180.0f;
    float c = cosf(rad), s = sinf(rad);
    r[0] = c; r[1] = s; r[4] = -s; r[5] = c;
    mat4_mul(out, m, r);
}

// out = m * rotate_y(angle_deg)
static inline void mat4_rotate_y(float out[16], const float m[16], float angle_deg) {
    float r[16]; mat4_identity(r);
    float rad = angle_deg * (float)M_PI / 180.0f;
    float c = cosf(rad), s = sinf(rad);
    r[0] = c; r[2] = -s; r[8] = s; r[10] = c;
    mat4_mul(out, m, r);
}

// out = m * scale(sx,sy,sz)
static inline void mat4_scale(float out[16], const float m[16],
                               float sx, float sy, float sz) {
    float s[16]; mat4_identity(s);
    s[0] = sx; s[5] = sy; s[10] = sz;
    mat4_mul(out, m, s);
}

// Orthographic projection matrix (matching glOrtho)
static inline void mat4_ortho(float m[16],
                               float l, float r, float b, float t,
                               float n, float f) {
    memset(m, 0, 16 * sizeof(float));
    m[0]  =  2.0f / (r - l);
    m[5]  =  2.0f / (t - b);
    m[10] = -2.0f / (f - n);
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[14] = -(f + n) / (f - n);
    m[15] = 1.0f;
}

// Perspective projection matrix (matching gluPerspective)
static inline void mat4_perspective(float m[16], float fovy_deg, float aspect,
                                     float near_z, float far_z) {
    memset(m, 0, 16 * sizeof(float));
    float f = 1.0f / tanf(fovy_deg * (float)M_PI / 360.0f);
    m[0]  = f / aspect;
    m[5]  = f;
    m[10] = (far_z + near_z) / (near_z - far_z);
    m[11] = -1.0f;
    m[14] = 2.0f * far_z * near_z / (near_z - far_z);
}

// View matrix (matching gluLookAt)
static inline void mat4_lookat(float m[16],
    float ex, float ey, float ez,
    float cx, float cy, float cz,
    float ux, float uy, float uz) {
    float fx = cx-ex, fy = cy-ey, fz = cz-ez;
    float fl = sqrtf(fx*fx+fy*fy+fz*fz); fx/=fl; fy/=fl; fz/=fl;
    float rx = fy*uz-fz*uy, ry = fz*ux-fx*uz, rz = fx*uy-fy*ux;
    float rl = sqrtf(rx*rx+ry*ry+rz*rz); rx/=rl; ry/=rl; rz/=rl;
    float vx = ry*fz-rz*fy, vy = rz*fx-rx*fz, vz = rx*fy-ry*fx;
    m[0] =rx; m[1] =vx; m[2] =-fx; m[3] =0;
    m[4] =ry; m[5] =vy; m[6] =-fy; m[7] =0;
    m[8] =rz; m[9] =vz; m[10]=-fz; m[11]=0;
    m[12]=-(rx*ex+ry*ey+rz*ez);
    m[13]=-(vx*ex+vy*ey+vz*ez);
    m[14]= (fx*ex+fy*ey+fz*ez);
    m[15]=1;
}
