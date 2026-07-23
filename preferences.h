#pragma once

#include <string>

// User preferences persisted to an INI file in the SDL pref path.
// Each setting has a sensible default; missing keys in the file are silently
// ignored so old preference files remain valid after new settings are added.

// Internal key codes match the unsigned-char values passed through the game's
// keyboard dispatch: printable ASCII (0–127) plus GLUT special keys encoded as
// 128 + GLUT_KEY_* (e.g. F1 = 129, F8 = 136, F11 = 139).

// One game action's keyboard binding: up to two key aliases (slot 0 is the
// primary shown on the keymap, slot 1 an alternate; 0 = empty slot).
//
// INI compatibility both ways (Steam testers switch between the netplay and
// stable branches, which share preferences.ini): save_preferences() keeps the
// canonical line in the old single-value format ("p1_thrust=w") so OLDER
// builds still parse it, and writes the alternate on a separate
// "p1_thrust_alt=up" line older builds silently ignore ("none" = explicitly
// cleared). Loading, a bare single value replaces the primary and KEEPS the
// current (default) alternate — that is what every pre-multibind file
// contains, and clearing the alternate on upgrade would silently strip the
// arrow aliases from existing installs. A comma list ("thrust=w,up" /
// "w,none", no spaces) is also accepted for hand edits and sets the binding
// exactly as listed. Assigning a plain int replaces the whole binding (used
// by the P2 ctor defaults, which deliberately carry no arrows).
struct KeyBinding {
    int keys[2];
    KeyBinding(int primary = 0, int alt = 0) : keys{primary, alt} {}
    bool matches(unsigned char key) const {
        return (keys[0] != 0 && key == (unsigned char)keys[0]) ||
               (keys[1] != 0 && key == (unsigned char)keys[1]);
    }
    int primary() const { return keys[0]; }
};

struct PlayerKeys {
    // P1's directions carry the arrow keys as built-in alternates (encoded
    // 128 + GLUT_KEY_*, like the F-keys); the P2 ctor overrides assign bare
    // ints, which clears the alternates so arrows drive player 1 only.
    KeyBinding left               = {'a', 128 + 100}; // + left arrow
    KeyBinding right              = {'d', 128 + 102}; // + right arrow
    KeyBinding thrust             = {'w', 128 + 101}; // + up arrow
    KeyBinding shoot              = ' ';
    KeyBinding reverse            = {'s', 128 + 103}; // + down arrow
    KeyBinding mine               = 'x';
    KeyBinding next_weapon        = 'q';
    KeyBinding next_secondary     = 'c';
    KeyBinding boost              = 'e';
    KeyBinding teleport           = 't';
    KeyBinding help               = 129; // F1  (128 + GLUT_KEY_F1)
    KeyBinding toggle_rotate_view = 'v'; // P1 default; P2 default is ';' (set in ctor)
    float keyboard_sensitivity = 1.0f;  // rotation speed multiplier
    float camera_smoothing     = 0.004f; // camera follow rate (0 = instant snap)
    bool  rotate_view          = true;  // camera follows this ship's heading
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
    bool fullscreen          = true;   // desktop only; ignored on mobile/web
    bool rotate_view         = true;   // LEGACY global (pre-per-player): only a
                                       // migration/downgrade seed now — the game
                                       // reads PlayerKeys::rotate_view per player
    bool friendly_fire       = true;   // players damage each other
    int  window_width        = 800;    // last windowed resolution (desktop only)
    int  window_height       = 600;
    float star_density       = 1.0f;   // star-count multiplier; user-editable in INI
    bool  lan_visible        = true;   // broadcast this host on the local network
                                       // (NETPLAY.md LAN play): when false the lobby
                                       // and mid-game re-host skip the UDP beacon +
                                       // TCP blob listener, so the machine is never
                                       // discoverable — the relay/manual flows are
                                       // untouched. INI-only (no Options row).
    std::string signal_url;            // netplay room server override (empty = baked default)
    std::string last_hosted_code;      // last room code this install hosted — the
                                       // lobby's clipboard auto-join refuses it, so a
                                       // killed-and-relaunched host can't walk into its
                                       // own dead room (typing it manually still works)
    PlayerKeys  p1_keys;          // player 1 keyboard bindings (p1 defaults)
    PlayerKeys  p2_keys;          // player 2 keyboard bindings (p2 defaults set in ctor)
    GeneralKeys general_keys;

    Preferences(); // sets p2_keys to player-2 defaults
};

// Returns the star-count multiplier from g_prefs (clamped to a safe range).
float star_density_scale();

// Canonical lowercase name for a named special key ("space", "escape",
// "left", ...), NULL when the code has no name (printable ASCII, F-keys and
// the decimal fallback are handled by the callers). THE one code→name table:
// the INI serializer/parser and the keymap HUD's labels all read it, so a
// new special key is added here once.
const char *special_key_name(int key);

// Populate g_prefs from disk.  Call once at startup (after the pref path is
// available, i.e. after IDBFS sync on web).  Returns defaults when no file
// exists or a key is absent.
void load_preferences();

// Write g_prefs to disk immediately.  On web, also flushes to IndexedDB.
void save_preferences();

// Global preferences instance.  Read/write it directly; call save_preferences()
// to persist changes.
extern Preferences g_prefs;
