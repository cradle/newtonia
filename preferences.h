#pragma once

// User preferences persisted to an INI file in the SDL pref path.
// Each setting has a sensible default; missing keys in the file are silently
// ignored so old preference files remain valid after new settings are added.

// Internal key codes match the unsigned-char values passed through the game's
// keyboard dispatch: printable ASCII (0–127) plus GLUT special keys encoded as
// 128 + GLUT_KEY_* (e.g. F1 = 129, F8 = 136, F11 = 139).

struct PlayerKeys {
    int left               = 'a';
    int right              = 'd';
    int thrust             = 'w';
    int shoot              = ' ';
    int reverse            = 's';
    int mine               = 'x';
    int next_weapon        = 'q';
    int next_secondary     = 'c';
    int boost              = 'e';
    int teleport           = 't';
    int help               = 129; // F1  (128 + GLUT_KEY_F1)
    int toggle_rotate_view = 'v'; // P1 default; P2 default is ';' (set in ctor)
};

struct GeneralKeys {
    int pause                = 'p';
    int menu                 = 27;  // Escape
    int add_player2          = 13;  // Return
    int toggle_friendly_fire = 'g';
    int skip_level           = 'n';
    int toggle_debug_grid    = 'b';
    int time_speed_up        = '=';
    int time_slow_down       = '-';
    int time_reset           = '0';
    int toggle_fullscreen    = 'f';
};

struct Preferences {
    bool fullscreen     = true;   // desktop only; ignored on mobile/web
    bool rotate_view    = true;   // camera follows ship heading
    bool friendly_fire  = true;   // players damage each other
    int  window_width   = 800;    // last windowed resolution (desktop only)
    int  window_height  = 600;

    PlayerKeys  p1_keys;          // player 1 keyboard bindings (p1 defaults)
    PlayerKeys  p2_keys;          // player 2 keyboard bindings (p2 defaults set in ctor)
    GeneralKeys general_keys;

    Preferences(); // sets p2_keys to player-2 defaults
};

// Populate g_prefs from disk.  Call once at startup (after the pref path is
// available, i.e. after IDBFS sync on web).  Returns defaults when no file
// exists or a key is absent.
void load_preferences();

// Write g_prefs to disk immediately.  On web, also flushes to IndexedDB.
void save_preferences();

// Global preferences instance.  Read/write it directly; call save_preferences()
// to persist changes.
extern Preferences g_prefs;
