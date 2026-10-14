#pragma once
// Offscreen render target for the desktop RESOLUTION option
// (port_display.h). When the player picks a height other than the window's
// own, glut.cpp binds this before the game draws, so every glViewport and
// projection the game sets up (all sized from the window size the game was
// handed) lands in a buffer of the picked size; present() then scales it
// up onto the real window with a linear blit: a cheaper render for a slow
// GPU or a big screen. glut.cpp never asks for more than the window's size.
//
// Desktop OpenGL 3.3 core only (framebuffer objects and glBlitFramebuffer);
// the other platforms never compile it.

class RenderTarget {
public:
  // Make the buffer w x h (keeping it if it already is). False if the
  // driver refuses it, in which case nothing is bound and the caller draws
  // straight to the window.
  static bool ensure(int w, int h);
  // Drop the buffer: the game draws to the window directly again.
  static void release();
  static bool active();
  // Draw into the buffer (call before the game draws).
  static void bind();
  // Scale the buffer onto the default framebuffer (window_w x window_h)
  // and leave the default framebuffer bound for the swap.
  static void present(int window_w, int window_h);
};
