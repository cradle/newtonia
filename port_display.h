#ifndef PORT_DISPLAY_H
#define PORT_DISPLAY_H

// Render resolution hooks. A build whose entry point can draw at a size
// other than the screen's (desktop: an offscreen buffer scaled to the
// window; the Xbox UWP port: a capped ANGLE surface the console scales up)
// defines NEWTONIA_PORT_RESOLUTION and supplies the port_* functions below.
// The Options screen then shows a RESOLUTION row over
// Preferences::render_height (NATIVE / 720P / 1080P / 1440P / 4K) and calls
// port_render_resolution_changed() each time the row changes; the port reads
// the pref and resizes what it draws into. It runs from inside the menu's
// input handling, so a port that has to rebuild a surface should note the
// request and do the work between frames. If the new size can't be made,
// the port puts render_height back (and saves it): the row reads the pref,
// so it shows what is really in force. Builds without the macro show no row
// and never call any of this.
//
// The desktop entry point (glut.cpp) is every build that reaches here with
// none of the other platforms' macros, so it is switched on here rather than
// in each build script that compiles it (the Makefile, the workflows'
// own command lines). Ports define it in their own build.
#if !defined(__ANDROID__) && !defined(__IOS__) && !defined(__EMSCRIPTEN__) && \
    !defined(_GAMING_XBOX) && !defined(_GAMING_DESKTOP) && \
    !defined(NEWTONIA_PORT_RESOLUTION)
#  define NEWTONIA_PORT_RESOLUTION
#endif

#if defined(NEWTONIA_PORT_RESOLUTION)
void port_render_resolution_changed();

// Also supplied by the port, for the line the Options screen draws under the
// list: the size the game is drawing at right now, and the tallest render
// that would draw anything more (0: no limit) — the screen's or window's own
// height. A taller pick draws at that size: on a 1080-line screen 1440P and
// 4K draw what 1080P does. The row still cycles and the pref still saves,
// so the pick takes effect on a screen that can show it, and the line says
// why nothing changed.
void port_render_size(int *w, int *h);
int  port_render_max_height();

// The port calls this when the height the player picked could not be made
// and it went back to the previous one (wanted_height: the one that failed,
// 0 = native; render_height already holds the one in force). The menu shows
// a dimmed notice over everything until any input dismisses it. Supplied by
// the shared code (menu.cpp), not by the port.
void port_render_resolution_failed(int wanted_height);
#endif

#endif
