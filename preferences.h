#pragma once

#include <string>

// User preferences persisted to an INI file in the SDL pref path.
// Each setting has a sensible default; missing keys in the file are silently
// ignored so old preference files remain valid after new settings are added.

// Internal key codes match the unsigned-char values passed through the game's
// keyboard dispatch: printable ASCII (0–127) plus GLUT special keys encoded as
// 128 + GLUT_KEY_* (e.g. F1 = 129, F8 = 136, F11 = 139).

// Local co-op player cap (FOURPLAYER.md D1). Lives here because every
// per-player slot in the game — key bindings, options rows, pad registries —
// sizes off the preference slots.
const int MAX_PLAYERS = 4;

// The live local seat cap. Held at 2 through the Phase A dark launch
// (FOURPLAYER.md §3) and flipped to MAX_PLAYERS once every piece —
// renderer, joins, saves, pads, tests — had landed and been verified.
// Netplay seats are still capped at 2 independently (Phase B).
const int LOCAL_PLAYER_CAP = MAX_PLAYERS;

// The live ONLINE seat cap — the Phase B dark-launch gate (FOURPLAYER.md
// §4), the netplay twin of LOCAL_PLAYER_CAP above. Held at 2 while the
// B1–B6 plumbing (NetPeer fan-out, PROTO 25, worker multi-join, lobby
// waiting room, per-seat resume, e2e) lands inert on the 2P wire;
// flipped to MAX_PLAYERS at B7 once the whole chain is verified.
const int NET_PLAYER_CAP = 2;

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
    // Auto-record replays (REPLAY.md). Default ON since 2026-07-28: the
    // low-end field pass cleared the recorder on real hardware across all
    // four axes (Moto E14 for CPU/RAM/lifecycle, Moto G05 for storage
    // flush — TESTING.md §7), which was the condition the original
    // record-silently design was held back on.
    //
    // Flipping the default alone is deliberately the WHOLE migration.
    // save_preferences() writes every key, so anyone who has already
    // launched the game carries an explicit `auto_record_replays=0` from
    // the old default and keeps it; only a fresh install (no INI, or one
    // predating the key) takes the new default. A migration marker to
    // sweep those zeros to ON was considered and rejected — it would
    // override exactly the people the rule is meant to leave alone.
    //
    // Reachable from Options ("RECORD REPLAYS", last row); NEWTONIA_REPLAY_ENABLE
    // forces it on and NEWTONIA_REPLAY_DISABLE off, both overriding the
    // pref. The recorder checks the result at game start
    // (GLGame::replay_start).
    bool auto_record_replays = true;
    // How a qualifying new personal best is handled at game over
    // (LEADERBOARD.md). ON (ASK) offers "UPLOAD TO LEADERBOARD?" — the
    // per-run opt-out; OFF (AUTO) uploads it straight away, showing the
    // same UPLOADING/UPLOADED status text on the card. Never "don't
    // upload": the LEADERBOARD screen's explicit upload action also
    // always works.
    bool leaderboard_prompts = true;
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
    // Per-player key bindings + camera scalars, slot 0 = player 1. The ctor
    // gives slot 1 the IJKL layout and ships slots 2–3 keyboard-inert
    // (FOURPLAYER.md D3: P3/P4 join by controller; empty bindings match
    // nothing, and p3_*/p4_* INI lines can still hand-bind them). NOTE for
    // downgrades: an older build rewriting the INI drops the p3_/p4_ lines —
    // the whole-file rewrite in save_preferences() only writes slots it
    // knows about.
    PlayerKeys  player_keys[MAX_PLAYERS];
    GeneralKeys general_keys;

    Preferences(); // sets slot 1 to player-2 defaults, clears slots 2-3's keys
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
