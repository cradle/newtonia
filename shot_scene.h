#ifndef SHOT_SCENE_H
#define SHOT_SCENE_H

// Screenshot harness (NEWTONIA_SHOT; see shots/README.md).
//
// Renders one composed scene — the menu, or a game world built to order
// (asteroids of any special type, hazards, enemies, pickups, text
// captions) — at an arbitrary window size, writes it to a PNG/BMP, and
// exits. Driven entirely by environment variables plus an optional scene
// script, so store/marketing shots are reproducible from the command line:
//
//   NEWTONIA_SHOT=out.png            enable; output path (.png or .bmp)
//   NEWTONIA_SHOT_SIZE=1920x1080     window size (default: preferences)
//   NEWTONIA_SHOT_SCENE=hero.shot    scene script (default: plain new game)
//   NEWTONIA_SHOT_MS=1500            simulated ms before capture
//
// This class is the platform-neutral core: parsing, world mutation (a
// friend of GLGame), text overlays, and the pixel capture/encode. The
// desktop entry point (glut.cpp) owns the window and the frame loop; other
// platforms compile this TU but never call it.
//
// Shot games can never touch real player data: replay recording is
// disabled, save_progress/high-score/savegame-delete are latched off, and
// the game is marked cheated so achievements and lifetime stats stay cold.

class State;

class ShotScene {
public:
  // True when NEWTONIA_SHOT is set — the desktop entry point then runs the
  // screenshot loop instead of the interactive game.
  static bool requested();
  // Parse the env vars and the scene script. false = bad input (logged).
  static bool init();
  static int width();   // requested window size; 0 = use preferences
  static int height();
  static int sim_ms();  // simulated time to run before capture
  // Build the configured state (Menu, or GLGame plus scene mutations).
  // Needs a live GL context — the constructors upload meshes.
  static State *build_state();
  // Deliver scene `key` events that have come due at sim time t_ms.
  static void pump_keys(State *state, int t_ms);
  // Draw the scene's `text` captions over the whole window.
  static void draw_overlays(int window_w, int window_h);
  // glReadPixels the back buffer and write the output file. Logs shot: lines.
  static bool capture(int window_w, int window_h);
  // One "shot: player N alive=..." line per player at capture time, so a
  // driver can assert the composed cast survived the sim without eyeballing
  // every render.
  static void log_state(State *state);
};

#endif
