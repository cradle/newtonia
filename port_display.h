#ifndef PORT_DISPLAY_H
#define PORT_DISPLAY_H

// Render resolution hook for ports. A port whose platform can render at
// more than one size (the Xbox UWP build: 1080p or the TV's 4K) defines
// NEWTONIA_PORT_RESOLUTION and supplies this function. The Options screen
// then shows a RESOLUTION row over Preferences::render_4k and calls it each
// time the row changes; the port reads the pref and resizes its render
// surface. It runs from inside the menu's input handling, so a port that
// has to rebuild a surface should note the request and do the work between
// frames. If the new size can't be made, the port puts render_4k back (and
// saves it): the row reads the pref, so it shows what is really in force.
// Builds without the macro show no row and never call it.
#if defined(NEWTONIA_PORT_RESOLUTION)
void port_render_resolution_changed();

// The port calls this when the size the player picked could not be made and
// it went back to the previous one (wanted_4k: the size that failed). The
// menu shows a dimmed notice over everything until any input dismisses it.
// Supplied by the shared code (menu.cpp), not by the port.
void port_render_resolution_failed(bool wanted_4k);
#endif

#endif
