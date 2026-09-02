#include "web_fs.h"
#include "preferences.h"
#include "audio_volume.h"
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cmath>
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
    // Slot 0 and general_keys use their struct default member initializers.
    // Override slot 1 with player-2 defaults.
    PlayerKeys &p2 = player_keys[1];
    p2.left           = 'j';
    p2.right          = 'l';
    p2.thrust         = 'i';
    p2.shoot          = '/';
    p2.reverse        = 'k';
    p2.mine           = ',';
    p2.next_weapon    = 'u';
    p2.next_secondary = '.';
    p2.boost          = 'o';
    p2.teleport       = 'y';
    p2.help               = 136; // F8  (128 + GLUT_KEY_F8)
    p2.toggle_rotate_view = ';'; // right of L, within IJKL cluster
    p2.zoom_in            = '9'; // number row above I/O; higher digit zooms in
    p2.zoom_out           = '8';
    // Slots 2+ ship keyboard-inert (FOURPLAYER.md D3): the keyboard has no
    // room for two more clusters, so P3/P4 join by controller. Scalars keep
    // the slot-0 defaults; p3_*/p4_* INI lines can still bind keys by hand.
    for (int i = 2; i < MAX_PLAYERS; i++) {
        PlayerKeys &pk = player_keys[i];
        pk.left = pk.right = pk.thrust = pk.shoot = pk.reverse = pk.mine = 0;
        pk.next_weapon = pk.next_secondary = pk.boost = pk.teleport = 0;
        pk.help = pk.toggle_rotate_view = 0;
        pk.zoom_in = pk.zoom_out = 0;
    }
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

// The one named-special-key table (see special_key_name in preferences.h).
// The HUD's key_label (glship.cpp) uppercases these same names, so a key
// added here shows up correctly in the keymap for free.
struct SpecialKey { int code; const char *name; };
static const SpecialKey kSpecialKeys[] = {
    { ' ',       "space"  },
    { 27,        "escape" },
    { 13,        "return" },
    { 9,         "tab"    },
    { 128 + 100, "left"   },  // 128 + GLUT_KEY_LEFT
    { 128 + 101, "up"     },  // 128 + GLUT_KEY_UP
    { 128 + 102, "right"  },  // 128 + GLUT_KEY_RIGHT
    { 128 + 103, "down"   },  // 128 + GLUT_KEY_DOWN
};

const char *special_key_name(int key) {
    for (size_t i = 0; i < sizeof(kSpecialKeys) / sizeof(kSpecialKeys[0]); i++)
        if (kSpecialKeys[i].code == key) return kSpecialKeys[i].name;
    return NULL;
}

static int special_key_code(const char *name) {
    for (size_t i = 0; i < sizeof(kSpecialKeys) / sizeof(kSpecialKeys[0]); i++)
        if (SDL_strcasecmp(name, kSpecialKeys[i].name) == 0)
            return kSpecialKeys[i].code;
    return 0;
}

// Serialise a key code to a human-readable INI value:
//   printable ASCII  → the character itself (e.g. "a", "/", "=")
//   named specials   → their kSpecialKeys name ("space", "up", ...)
//   F1–F12 (129–140) → "F1"–"F12"
//   anything else    → decimal integer (fallback)
static std::string key_to_ini(int key) {
    // An empty slot must write "none", never the decimal fallback's "0":
    // ini_to_key parses "0" as the literal '0' key, so a zeroed binding
    // (P3/P4 ship keyboard-inert) would come back bound after one
    // save/load cycle. ini_to_key already maps "none" to 0, on old builds
    // too.
    if (key == 0) return "none";
    if (const char *n = special_key_name(key)) return n;
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

    if (int k = special_key_code(val)) return k;
    // Hand-edit aliases and the explicit empty slot (not canonical names,
    // so key_to_ini never writes them).
    if (SDL_strcasecmp(val, "esc")    == 0) return 27;
    if (SDL_strcasecmp(val, "enter")  == 0) return 13;
    if (SDL_strcasecmp(val, "none")   == 0) return 0;

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

// Map a "pN_" INI name prefix (N = 1..MAX_PLAYERS) to that player's slot;
// NULL for any other name.
static PlayerKeys *player_keys_for_prefix(const char *name) {
    if (name[0] == 'p' && name[1] >= '1' && name[1] < '1' + MAX_PLAYERS &&
        name[2] == '_')
        return &g_prefs.player_keys[name[1] - '1'];
    return NULL;
}

// Map an INI binding name ("p1_thrust", "p4_mine", ...) to its KeyBinding.
// Returns NULL for anything else (p1_keyboard_sensitivity etc. fall through
// to the scalar rows in parse_line).
static KeyBinding *binding_for(const char *name) {
    PlayerKeys *pk = player_keys_for_prefix(name);
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
    if (strcmp(a, "zoom_in")            == 0) return &pk->zoom_in;
    if (strcmp(a, "zoom_out")           == 0) return &pk->zoom_out;
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
        // Legacy single global (pre-per-player). Seed EVERY player from it so
        // an old INI's one setting migrates; explicit pN_rotate_view lines
        // (written after this in newer files) override per player.
        bool v = (val[0] == '1');
        g_prefs.rotate_view = v;
        for (int i = 0; i < MAX_PLAYERS; i++)
            g_prefs.player_keys[i].rotate_view = v;
    } else if (strcmp(key, "friendly_fire") == 0) {
        g_prefs.friendly_fire = (val[0] == '1');
    } else if (strcmp(key, "allow_anonymous") == 0) {
        g_prefs.allow_anonymous = (val[0] == '1');
    } else if (strcmp(key, "boost_hint_done") == 0) {
        g_prefs.boost_hint_done = (val[0] == '1');
    } else if (strcmp(key, "auto_record_replays") == 0) {
        g_prefs.auto_record_replays = (val[0] == '1');
    } else if (strcmp(key, "leaderboard_prompts") == 0) {
        g_prefs.leaderboard_prompts = (val[0] == '1');
    } else if (strcmp(key, "star_density") == 0) {
        float v = (float)atof(val);
        if (v >= 0.0f) g_prefs.star_density = v;
    } else if (strcmp(key, "master_volume") == 0) {
        float v = (float)atof(val);
        if (v >= 0.0f && v <= 1.0f) g_prefs.master_volume = v;
    } else if (strcmp(key, "music_volume") == 0) {
        float v = (float)atof(val);
        if (v >= 0.0f && v <= 1.0f) g_prefs.music_volume = v;
    } else if (strcmp(key, "lan_visible") == 0) {
        g_prefs.lan_visible = (val[0] == '1');
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
    // Per-player scalars, any pN_ slot (bindings are handled generically
    // above, so a pN_ name reaching here is a scalar or unknown)
    } else if (PlayerKeys *pk = player_keys_for_prefix(key)) {
        const char *a = key + 3;
        if (strcmp(a, "keyboard_sensitivity") == 0) {
            float v = (float)atof(val);
            if (v >= 0.1f && v <= 5.0f) pk->keyboard_sensitivity = v;
        } else if (strcmp(a, "camera_smoothing") == 0) {
            float v = (float)atof(val);
            if (v >= 0.0f && v <= 0.1f) pk->camera_smoothing = v;
        } else if (strcmp(a, "rotate_view") == 0) {
            pk->rotate_view = (val[0] == '1');
        } else if (strcmp(a, "camera_zoom") == 0) {
            float v = (float)atof(val);
            if (v >= 0.5f && v <= 2.0f) pk->camera_zoom = v;
        } else if (strcmp(a, "speed_zoom") == 0) {
            float v = (float)atof(val);
            if (v >= 0.0f && v <= 1.0f) pk->speed_zoom = v;
        }

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
    // Push the loaded volume levels onto the mixer. NOT sufficient at
    // startup on its own: the entry points load preferences BEFORE they
    // open audio, and Mix_OpenAudio resets the music volume — the Menu
    // constructor re-applies at the first certainly-after-open point.
    // This call covers pref reloads once audio is up. The early returns
    // above skip it deliberately: struct defaults (full volume) match
    // the mixer's own defaults, so there is nothing to push.
    AudioVolume::apply();
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
    fprintf(f, "rotate_view=%d\n",             g_prefs.player_keys[0].rotate_view ? 1 : 0);
    fprintf(f, "friendly_fire=%d\n",           g_prefs.friendly_fire      ? 1 : 0);
    fprintf(f, "allow_anonymous=%d\n",         g_prefs.allow_anonymous    ? 1 : 0);
    fprintf(f, "boost_hint_done=%d\n",         g_prefs.boost_hint_done    ? 1 : 0);
    fprintf(f, "auto_record_replays=%d\n",     g_prefs.auto_record_replays ? 1 : 0);
    fprintf(f, "leaderboard_prompts=%d\n",     g_prefs.leaderboard_prompts ? 1 : 0);
    fprintf(f, "star_density=%.4f\n",           g_prefs.star_density);
    fprintf(f, "master_volume=%.4f\n",          g_prefs.master_volume);
    fprintf(f, "music_volume=%.4f\n",           g_prefs.music_volume);
    fprintf(f, "lan_visible=%d\n",             g_prefs.lan_visible        ? 1 : 0);
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
// Expects the enclosing player loop's `p` (1-based slot number) in scope.
// key_to_ini writes empty slots as "none" (primary and alternate alike).
#define WRITE_PLAYER_BINDING(action, b) \
    fprintf(f, "p%d_" action "=%s\n" "p%d_" action "_alt=%s\n", \
        p, key_to_ini((b).keys[0]).c_str(), \
        p, key_to_ini((b).keys[1]).c_str())

    // Per-player keybinds + scalars, one block per slot (p1..p4). p3_/p4_
    // lines are unknown keys to older builds (ignored on load, dropped on
    // their whole-file rewrite — see the player_keys note in preferences.h).
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const PlayerKeys &pk = g_prefs.player_keys[i];
        const int p = i + 1;
        WRITE_PLAYER_BINDING("left",           pk.left);
        WRITE_PLAYER_BINDING("right",          pk.right);
        WRITE_PLAYER_BINDING("thrust",         pk.thrust);
        WRITE_PLAYER_BINDING("shoot",          pk.shoot);
        WRITE_PLAYER_BINDING("reverse",        pk.reverse);
        WRITE_PLAYER_BINDING("mine",           pk.mine);
        WRITE_PLAYER_BINDING("next_weapon",    pk.next_weapon);
        WRITE_PLAYER_BINDING("next_secondary", pk.next_secondary);
        WRITE_PLAYER_BINDING("boost",          pk.boost);
        WRITE_PLAYER_BINDING("teleport",       pk.teleport);
        WRITE_PLAYER_BINDING("help",               pk.help);
        WRITE_PLAYER_BINDING("toggle_rotate_view", pk.toggle_rotate_view);
        WRITE_PLAYER_BINDING("zoom_in",            pk.zoom_in);
        WRITE_PLAYER_BINDING("zoom_out",           pk.zoom_out);
        fprintf(f, "p%d_keyboard_sensitivity=%.2f\n", p, pk.keyboard_sensitivity);
        fprintf(f, "p%d_camera_smoothing=%.4f\n",     p, pk.camera_smoothing);
        fprintf(f, "p%d_rotate_view=%d\n",            p, pk.rotate_view ? 1 : 0);
        fprintf(f, "p%d_camera_zoom=%.2f\n",          p, pk.camera_zoom);
        fprintf(f, "p%d_speed_zoom=%.2f\n",           p, pk.speed_zoom);
    }

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
#undef WRITE_PLAYER_BINDING

    fclose(f);

    // Persist to IndexedDB so preferences survive a page refresh.
    web_fs_sync("preferences");
}

const float CAMERA_ZOOM_VALUES[CAMERA_ZOOM_STEPS] = {0.8f, 0.9f, 1.0f, 1.1f, 1.2f};
const char *const CAMERA_ZOOM_LABELS[CAMERA_ZOOM_STEPS] = {
    "CLOSEST", "CLOSE", "NORMAL", "WIDE", "WIDEST"};

int camera_zoom_index(float value) {
    int best = 2;
    float best_d = 1e6f;
    for (int i = 0; i < CAMERA_ZOOM_STEPS; i++) {
        float d = fabsf(value - CAMERA_ZOOM_VALUES[i]);
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}
