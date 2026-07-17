#include "preferences.h"
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

Preferences g_prefs;

float star_density_scale() {
    float v = g_prefs.star_density;
    if (v < 0.0f) v = 0.0f;
    return v;
}

Preferences::Preferences() {
    // p1_keys and general_keys use their struct default member initializers.
    // Override p2_keys with player-2 defaults.
    p2_keys.left           = 'j';
    p2_keys.right          = 'l';
    p2_keys.thrust         = 'i';
    p2_keys.shoot          = '/';
    p2_keys.reverse        = 'k';
    p2_keys.mine           = ',';
    p2_keys.next_weapon    = 'u';
    p2_keys.next_secondary = '.';
    p2_keys.boost          = 'o';
    p2_keys.teleport       = 'y';
    p2_keys.help               = 136; // F8  (128 + GLUT_KEY_F8)
    p2_keys.toggle_rotate_view = ';'; // right of L, within IJKL cluster
}

static const char* PREF_ORG  = "cc.gfm";
static const char* PREF_APP  = "newtonia";
static const char* PREF_FILE = "preferences.ini";

static std::string pref_filepath() {
    char *path = SDL_GetPrefPath(PREF_ORG, PREF_APP);
    if (!path) return "";
    std::string fp = std::string(path) + PREF_FILE;
    SDL_free(path);
    return fp;
}

// Serialise a key code to a human-readable INI value:
//   printable ASCII  → the character itself (e.g. "a", "/", "=")
//   space (32)       → "space"
//   escape (27)      → "escape"
//   return (13)      → "return"
//   tab (9)          → "tab"
//   F1–F12 (129–140) → "F1"–"F12"
//   arrows (228–231, i.e. 128 + GLUT_KEY_*) → "up"/"down"/"left"/"right"
//   anything else    → decimal integer (fallback)
static std::string key_to_ini(int key) {
    switch (key) {
        case ' ':  return "space";
        case 27:   return "escape";
        case 13:   return "return";
        case 9:    return "tab";
        case 128 + 100: return "left";   // GLUT_KEY_LEFT
        case 128 + 101: return "up";     // GLUT_KEY_UP
        case 128 + 102: return "right";  // GLUT_KEY_RIGHT
        case 128 + 103: return "down";   // GLUT_KEY_DOWN
    }
    if (key >= 129 && key <= 140) {
        char buf[8];
        snprintf(buf, sizeof(buf), "F%d", key - 128);
        return buf;
    }
    if (key >= 33 && key < 127)
        return std::string(1, (char)key);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", key);
    return buf;
}

// Parse an INI value back to a key code.  Case-insensitive for named keys.
// Also accepts bare decimal integers so old files remain valid.
static int ini_to_key(const char *val) {
    if (!val || !val[0]) return 0;

    if (SDL_strcasecmp(val, "space")  == 0) return ' ';
    if (SDL_strcasecmp(val, "escape") == 0) return 27;
    if (SDL_strcasecmp(val, "esc")    == 0) return 27;
    if (SDL_strcasecmp(val, "return") == 0) return 13;
    if (SDL_strcasecmp(val, "enter")  == 0) return 13;
    if (SDL_strcasecmp(val, "tab")    == 0) return 9;
    if (SDL_strcasecmp(val, "left")   == 0) return 128 + 100; // GLUT_KEY_LEFT
    if (SDL_strcasecmp(val, "up")     == 0) return 128 + 101; // GLUT_KEY_UP
    if (SDL_strcasecmp(val, "right")  == 0) return 128 + 102; // GLUT_KEY_RIGHT
    if (SDL_strcasecmp(val, "down")   == 0) return 128 + 103; // GLUT_KEY_DOWN
    if (SDL_strcasecmp(val, "none")   == 0) return 0;         // explicit empty slot

    // F1–F12 (case-insensitive prefix 'f' or 'F')
    if ((val[0] == 'F' || val[0] == 'f') && val[1] != '\0') {
        int n = atoi(val + 1);
        if (n >= 1 && n <= 12) return 128 + n;
    }

    // Single printable character
    if (val[1] == '\0' && (unsigned char)val[0] >= 33 && (unsigned char)val[0] < 127)
        return (unsigned char)val[0];

    return 0;
}

// Parse an INI value into a two-slot binding (see KeyBinding in
// preferences.h). Bare value ("w"): replace the primary and keep the
// existing (default) alternate — this is what every pre-multibind file
// contains, and clearing the alternate on upgrade would silently strip the
// arrow aliases from existing installs. Comma form ("w,up" / "w,none"): the
// binding becomes exactly the listed keys. No spaces around the comma (the
// INI line reader stops at whitespace).
static void ini_to_binding(const char *val, KeyBinding &b) {
    const char *comma = strchr(val, ',');
    if (!comma) {
        int k = ini_to_key(val);
        if (k) b.keys[0] = k;
        return;
    }
    std::string first(val, (size_t)(comma - val));
    b.keys[0] = ini_to_key(first.c_str());
    b.keys[1] = ini_to_key(comma + 1);
}

// Map an INI binding name ("p1_thrust", "p2_mine", ...) to its KeyBinding.
// Returns NULL for anything else (p1_keyboard_sensitivity etc. fall through
// to the scalar rows in parse_line).
static KeyBinding *binding_for(const char *name) {
    PlayerKeys *pk = NULL;
    if      (strncmp(name, "p1_", 3) == 0) pk = &g_prefs.p1_keys;
    else if (strncmp(name, "p2_", 3) == 0) pk = &g_prefs.p2_keys;
    if (!pk) return NULL;
    const char *a = name + 3;
    if (strcmp(a, "left")               == 0) return &pk->left;
    if (strcmp(a, "right")              == 0) return &pk->right;
    if (strcmp(a, "thrust")             == 0) return &pk->thrust;
    if (strcmp(a, "shoot")              == 0) return &pk->shoot;
    if (strcmp(a, "reverse")            == 0) return &pk->reverse;
    if (strcmp(a, "mine")               == 0) return &pk->mine;
    if (strcmp(a, "next_weapon")        == 0) return &pk->next_weapon;
    if (strcmp(a, "next_secondary")     == 0) return &pk->next_secondary;
    if (strcmp(a, "boost")              == 0) return &pk->boost;
    if (strcmp(a, "teleport")           == 0) return &pk->teleport;
    if (strcmp(a, "help")               == 0) return &pk->help;
    if (strcmp(a, "toggle_rotate_view") == 0) return &pk->toggle_rotate_view;
    return NULL;
}

// Parse a single key=value line from the INI file.
static void parse_line(const char *key, const char *val) {
    // Player key bindings, handled generically: "<pN>_<action>" sets the
    // binding (bare value or comma list — see ini_to_binding), and the
    // downgrade-safe companion line "<pN>_<action>_alt" (what
    // save_preferences() writes) sets just the alternate slot.
    {
        size_t len = strlen(key);
        if (len > 4 && strcmp(key + len - 4, "_alt") == 0) {
            std::string base(key, len - 4);
            if (KeyBinding *b = binding_for(base.c_str())) {
                b->keys[1] = ini_to_key(val);
                return;
            }
        } else if (KeyBinding *b = binding_for(key)) {
            ini_to_binding(val, *b);
            return;
        }
    }
    // Scalar preferences
    if (strcmp(key, "fullscreen") == 0) {
        g_prefs.fullscreen = (val[0] == '1');
    } else if (strcmp(key, "rotate_view") == 0) {
        // Legacy single global (pre-per-player). Seed BOTH players from it so
        // an old INI's one setting migrates; explicit p1_/p2_rotate_view lines
        // below (written after this in newer files) override per player.
        bool v = (val[0] == '1');
        g_prefs.rotate_view = v;
        g_prefs.p1_keys.rotate_view = v;
        g_prefs.p2_keys.rotate_view = v;
    } else if (strcmp(key, "friendly_fire") == 0) {
        g_prefs.friendly_fire = (val[0] == '1');
    } else if (strcmp(key, "star_density") == 0) {
        float v = (float)atof(val);
        if (v >= 0.0f) g_prefs.star_density = v;
    } else if (strcmp(key, "signal_url") == 0) {
        g_prefs.signal_url = val;
    } else if (strcmp(key, "last_hosted_code") == 0) {
        g_prefs.last_hosted_code = val;
    } else if (strcmp(key, "window_width") == 0) {
        int w = atoi(val);
        if (w > 0) g_prefs.window_width = w;
    } else if (strcmp(key, "window_height") == 0) {
        int h = atoi(val);
        if (h > 0) g_prefs.window_height = h;
    // Player 1 scalars (bindings are handled generically above)
    } else if (strcmp(key, "p1_keyboard_sensitivity") == 0) {
        float v = (float)atof(val);
        if (v >= 0.1f && v <= 5.0f) g_prefs.p1_keys.keyboard_sensitivity = v;
    } else if (strcmp(key, "p1_camera_smoothing") == 0) {
        float v = (float)atof(val);
        if (v >= 0.0f && v <= 0.1f) g_prefs.p1_keys.camera_smoothing = v;
    } else if (strcmp(key, "p1_rotate_view") == 0) {
        g_prefs.p1_keys.rotate_view = (val[0] == '1');

    // Player 2 scalars
    } else if (strcmp(key, "p2_keyboard_sensitivity") == 0) {
        float v = (float)atof(val);
        if (v >= 0.1f && v <= 5.0f) g_prefs.p2_keys.keyboard_sensitivity = v;
    } else if (strcmp(key, "p2_camera_smoothing") == 0) {
        float v = (float)atof(val);
        if (v >= 0.0f && v <= 0.1f) g_prefs.p2_keys.camera_smoothing = v;
    } else if (strcmp(key, "p2_rotate_view") == 0) {
        g_prefs.p2_keys.rotate_view = (val[0] == '1');

    // General keybinds
    } else if (strcmp(key, "general_pause")                == 0) { g_prefs.general_keys.pause                = ini_to_key(val);
    } else if (strcmp(key, "general_menu")                 == 0) { g_prefs.general_keys.menu                 = ini_to_key(val);
    } else if (strcmp(key, "general_add_player2")          == 0) { g_prefs.general_keys.add_player2          = ini_to_key(val);
    } else if (strcmp(key, "general_toggle_friendly_fire") == 0) { g_prefs.general_keys.toggle_friendly_fire = ini_to_key(val);
    } else if (strcmp(key, "general_skip_level")           == 0) { g_prefs.general_keys.skip_level           = ini_to_key(val);
    } else if (strcmp(key, "general_toggle_debug_grid")    == 0) { g_prefs.general_keys.toggle_debug_grid    = ini_to_key(val);
    } else if (strcmp(key, "general_time_speed_up")        == 0) { g_prefs.general_keys.time_speed_up        = ini_to_key(val);
    } else if (strcmp(key, "general_time_slow_down")       == 0) { g_prefs.general_keys.time_slow_down       = ini_to_key(val);
    } else if (strcmp(key, "general_time_reset")           == 0) { g_prefs.general_keys.time_reset           = ini_to_key(val);
    } else if (strcmp(key, "general_toggle_fullscreen")    == 0) { g_prefs.general_keys.toggle_fullscreen    = ini_to_key(val);
    }
    // Unknown keys are silently ignored so older files stay valid.
}

void load_preferences() {
    // Start from struct defaults.
    g_prefs = Preferences();

    std::string fp = pref_filepath();
    if (fp.empty()) return;

    FILE *f = fopen(fp.c_str(), "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        // Strip trailing newline / carriage return.
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        char key[128], val[128];
        if (sscanf(line, "%127[^=]=%127s", key, val) != 2) continue;
        parse_line(key, val);
    }
    fclose(f);
}

void save_preferences() {
    std::string fp = pref_filepath();
    if (fp.empty()) return;

    FILE *f = fopen(fp.c_str(), "w");
    if (!f) return;

    // Scalar preferences
    fprintf(f, "fullscreen=%d\n",              g_prefs.fullscreen         ? 1 : 0);
    // Legacy global written from P1 so a downgrade to a pre-per-player build
    // still reads a sane camera setting; new builds use p1_/p2_rotate_view.
    fprintf(f, "rotate_view=%d\n",             g_prefs.p1_keys.rotate_view ? 1 : 0);
    fprintf(f, "friendly_fire=%d\n",           g_prefs.friendly_fire      ? 1 : 0);
    fprintf(f, "star_density=%.4f\n",           g_prefs.star_density);
    if (!g_prefs.signal_url.empty())
        fprintf(f, "signal_url=%s\n",             g_prefs.signal_url.c_str());
    if (!g_prefs.last_hosted_code.empty())
        fprintf(f, "last_hosted_code=%s\n",       g_prefs.last_hosted_code.c_str());
    fprintf(f, "window_width=%d\n",            g_prefs.window_width);
    fprintf(f, "window_height=%d\n",           g_prefs.window_height);

#define WRITE_KEY(name, val) fprintf(f, name "=%s\n", key_to_ini(val).c_str())
// Bindings write downgrade-safe: the canonical line carries only the primary
// (older builds sharing this file — Steam branch switches — parse it as
// before), and the alternate rides a "_alt" line older builds ignore.
// An empty alternate is written explicitly as "none" so a cleared alternate
// is not resurrected by the bare-value default-keeping rule on the next load.
#define WRITE_BINDING(name, b) fprintf(f, name "=%s\n" name "_alt=%s\n", \
        key_to_ini((b).keys[0]).c_str(), \
        (b).keys[1] ? key_to_ini((b).keys[1]).c_str() : "none")

    // Player 1 keybinds
    WRITE_BINDING("p1_left",           g_prefs.p1_keys.left);
    WRITE_BINDING("p1_right",          g_prefs.p1_keys.right);
    WRITE_BINDING("p1_thrust",         g_prefs.p1_keys.thrust);
    WRITE_BINDING("p1_shoot",          g_prefs.p1_keys.shoot);
    WRITE_BINDING("p1_reverse",        g_prefs.p1_keys.reverse);
    WRITE_BINDING("p1_mine",           g_prefs.p1_keys.mine);
    WRITE_BINDING("p1_next_weapon",    g_prefs.p1_keys.next_weapon);
    WRITE_BINDING("p1_next_secondary", g_prefs.p1_keys.next_secondary);
    WRITE_BINDING("p1_boost",          g_prefs.p1_keys.boost);
    WRITE_BINDING("p1_teleport",       g_prefs.p1_keys.teleport);
    WRITE_BINDING("p1_help",               g_prefs.p1_keys.help);
    WRITE_BINDING("p1_toggle_rotate_view", g_prefs.p1_keys.toggle_rotate_view);
    fprintf(f, "p1_keyboard_sensitivity=%.2f\n", g_prefs.p1_keys.keyboard_sensitivity);
    fprintf(f, "p1_camera_smoothing=%.4f\n",     g_prefs.p1_keys.camera_smoothing);
    fprintf(f, "p1_rotate_view=%d\n",            g_prefs.p1_keys.rotate_view ? 1 : 0);

    // Player 2 keybinds
    WRITE_BINDING("p2_left",           g_prefs.p2_keys.left);
    WRITE_BINDING("p2_right",          g_prefs.p2_keys.right);
    WRITE_BINDING("p2_thrust",         g_prefs.p2_keys.thrust);
    WRITE_BINDING("p2_shoot",          g_prefs.p2_keys.shoot);
    WRITE_BINDING("p2_reverse",        g_prefs.p2_keys.reverse);
    WRITE_BINDING("p2_mine",           g_prefs.p2_keys.mine);
    WRITE_BINDING("p2_next_weapon",    g_prefs.p2_keys.next_weapon);
    WRITE_BINDING("p2_next_secondary", g_prefs.p2_keys.next_secondary);
    WRITE_BINDING("p2_boost",          g_prefs.p2_keys.boost);
    WRITE_BINDING("p2_teleport",       g_prefs.p2_keys.teleport);
    WRITE_BINDING("p2_help",               g_prefs.p2_keys.help);
    WRITE_BINDING("p2_toggle_rotate_view", g_prefs.p2_keys.toggle_rotate_view);
    fprintf(f, "p2_keyboard_sensitivity=%.2f\n", g_prefs.p2_keys.keyboard_sensitivity);
    fprintf(f, "p2_camera_smoothing=%.4f\n",     g_prefs.p2_keys.camera_smoothing);
    fprintf(f, "p2_rotate_view=%d\n",            g_prefs.p2_keys.rotate_view ? 1 : 0);

    // General keybinds
    WRITE_KEY("general_pause",                g_prefs.general_keys.pause);
    WRITE_KEY("general_menu",                 g_prefs.general_keys.menu);
    WRITE_KEY("general_add_player2",          g_prefs.general_keys.add_player2);
    WRITE_KEY("general_toggle_friendly_fire", g_prefs.general_keys.toggle_friendly_fire);
    WRITE_KEY("general_skip_level",           g_prefs.general_keys.skip_level);
    WRITE_KEY("general_toggle_debug_grid",    g_prefs.general_keys.toggle_debug_grid);
    WRITE_KEY("general_time_speed_up",        g_prefs.general_keys.time_speed_up);
    WRITE_KEY("general_time_slow_down",       g_prefs.general_keys.time_slow_down);
    WRITE_KEY("general_time_reset",           g_prefs.general_keys.time_reset);
    WRITE_KEY("general_toggle_fullscreen",    g_prefs.general_keys.toggle_fullscreen);

#undef WRITE_KEY
#undef WRITE_BINDING

    fclose(f);

#ifdef __EMSCRIPTEN__
    // Flush the in-memory filesystem to IndexedDB so preferences survive page
    // refreshes.
    EM_ASM(
        FS.syncfs(false, function(err) {
            if (err) console.error('[newtonia] IDBFS pref save failed:', err);
        });
    );
#endif
}
