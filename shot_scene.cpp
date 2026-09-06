// Screenshot harness core (NEWTONIA_SHOT; see shot_scene.h and
// shots/README.md). Platform-neutral: the desktop entry point (glut.cpp)
// owns the window and the frame loop and is the only caller; every other
// platform compiles this TU inert.

#include "shot_scene.h"

#include "glgame.h"
#include "glenemy.h"
#include "follower.h"
#include "menu.h"
#include "achievements.h"
#include "preferences.h"
#include "typer.h"
#include "mat4.h"
#include "gl_compat.h"

#include <SDL.h>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct SceneAsteroid {
  float x = 0, y = 0;
  bool inv = false, invis = false, refl = false, tele = false;
  bool quant = false, tough = false, arm = false, phas = false;
  float r = 0;  bool has_r = false;
  float vx = 0, vy = 0;  bool has_v = false;
};
struct SceneEnemy  { float x = 0, y = 0, difficulty = 1.0f; };
struct SceneHazard {
  Hazard::Kind kind = Hazard::PULSAR;
  float x = 0, y = 0;
  float vx = 0, vy = 0;  bool has_v = false;
};
struct ScenePickup { std::string type; float x = 0, y = 0; };
struct SceneBlackHole { float x = 0, y = 0; };
struct SceneText   { float x = 0, y = 0, size = 8; std::string text; };
struct SceneKey {
  unsigned char key = 0;
  int at_ms = 0;
  bool hold = false;  // `hold`: press and keep held through capture
  bool downed = false, upped = false;
};
struct SceneTap {
  float nx = 0.5f, ny = 0.5f;  // 0..1, top-left origin (State::touch_tap)
  int at_ms = 0;
  bool sent = false;
};

struct Scene {
  int width = 0, height = 0;      // 0 = preferences
  int sim_ms = 1000;
  unsigned seed = 1337;           // srand() before build: same scene, same shot
  bool menu_mode = false;
  int generation = 0;             // game base: NEWTONIA_START_GENERATION path
  bool clear_world = false;
  bool hud = true;
  bool no_ship = false;  // hold every player unspawned: pure-scenery shots
  float star_density = -1;  // `stars`: overrides the preference; -1 = keep
  // `transparent`: write RGBA with black as full transparency (logo/text
  // assets). Alpha = the brightest channel, colour un-premultiplied, so
  // dim edge pixels become translucent instead of dark.
  bool transparent = false;
  int num_players = 1;            // `players N`, 1..MAX_PLAYERS local seats
  float zoom = 0;                 // vertical FOV degrees; 0 = default (85)
  // Camera mode. The game's default is ROTATE (view follows the ship's
  // heading, ship always drawn pointing up). FIXED keeps the world's
  // orientation on screen — WYSIWYG for composed scenes with angled ships.
  bool camera_rotate = true;
  // Per-seat placement: slot 0 = `ship`, 1..3 = `ship2`..`ship4`.
  bool  ship_set[MAX_PLAYERS] = {};
  float ship_x[MAX_PLAYERS] = {}, ship_y[MAX_PLAYERS] = {};
  bool  ship_angle_set[MAX_PLAYERS] = {};
  float ship_angle[MAX_PLAYERS] = {};
  std::vector<SceneAsteroid> asteroids;
  std::vector<SceneEnemy> enemies;
  std::vector<SceneHazard> hazards;
  std::vector<ScenePickup> pickups;
  std::vector<SceneBlackHole> black_holes;
  std::vector<SceneText> texts;
  std::vector<SceneKey> keys;
  std::vector<SceneTap> taps;
};

Scene s_scene;
std::string s_out_path;

bool parse_error(int line_no, const std::string &line, const char *why) {
  std::cout << "shot: scene line " << line_no << ": " << why << " -- \""
            << line << "\"" << std::endl;
  return false;
}

unsigned char key_from_name(const std::string &name) {
  if (name == "enter")  return '\r';
  if (name == "space")  return ' ';
  if (name == "esc")    return 27;
  // The desktop special-key scheme (State::nav_key): 128 + GLUT_KEY_*.
  if (name == "left")   return 128 + 100;
  if (name == "up")     return 128 + 101;
  if (name == "right")  return 128 + 102;
  if (name == "down")   return 128 + 103;
  if (name.size() == 1) return (unsigned char)name[0];
  return 0;
}

bool parse_scene_file(const char *path) {
  std::ifstream f(path);
  if (!f) {
    std::cout << "shot: cannot open scene file " << path << std::endl;
    return false;
  }
  std::string line;
  int line_no = 0;
  int next_key_ms = 200;  // default spacing for `key` without a time
  while (std::getline(f, line)) {
    line_no++;
    // Strip comments and blank lines.
    size_t hash = line.find('#');
    if (hash != std::string::npos) line.resize(hash);
    std::istringstream in(line);
    std::string cmd;
    if (!(in >> cmd)) continue;

    if (cmd == "size") {
      // "size 1920 1080" or "size 1920x1080"
      std::string rest;
      in >> rest;
      int w = 0, h = 0;
      if (sscanf(rest.c_str(), "%dx%d", &w, &h) != 2) {
        w = atoi(rest.c_str());
        if (!(in >> h)) h = 0;
      }
      if (w < 64 || h < 64) return parse_error(line_no, line, "bad size");
      s_scene.width = w;  s_scene.height = h;
    } else if (cmd == "sim") {
      if (!(in >> s_scene.sim_ms) || s_scene.sim_ms < 0)
        return parse_error(line_no, line, "bad sim ms");
    } else if (cmd == "seed") {
      if (!(in >> s_scene.seed)) return parse_error(line_no, line, "bad seed");
    } else if (cmd == "menu") {
      s_scene.menu_mode = true;
    } else if (cmd == "game") {
      s_scene.menu_mode = false;
      int gen = 0;
      if (in >> gen) s_scene.generation = gen;
    } else if (cmd == "clear") {
      s_scene.clear_world = true;
    } else if (cmd == "noship") {
      s_scene.no_ship = true;
    } else if (cmd == "transparent") {
      s_scene.transparent = true;
    } else if (cmd == "stars") {
      if (!(in >> s_scene.star_density) || s_scene.star_density < 0.0f ||
          s_scene.star_density > 1.0f)
        return parse_error(line_no, line, "stars wants 0..1 (density scale)");
    } else if (cmd == "hud") {
      std::string v;  in >> v;
      s_scene.hud = (v != "off");
    } else if (cmd == "players") {
      int n = 1;
      if (!(in >> n) || n < 1 || n > MAX_PLAYERS)
        return parse_error(line_no, line, "players must be 1..4");
      s_scene.num_players = n;
    } else if (cmd == "camera") {
      std::string v;  in >> v;
      if (v == "fixed")       s_scene.camera_rotate = false;
      else if (v == "rotate") s_scene.camera_rotate = true;
      else return parse_error(line_no, line, "camera fixed|rotate");
    } else if (cmd == "zoom") {
      if (!(in >> s_scene.zoom) || s_scene.zoom < 10 || s_scene.zoom > 170)
        return parse_error(line_no, line, "zoom must be 10..170 (FOV degrees)");
    } else if (cmd == "ship" || cmd == "ship2" || cmd == "ship3" ||
               cmd == "ship4") {
      int seat = (cmd == "ship") ? 0 : cmd[4] - '1';
      float x, y;
      if (!(in >> x >> y)) return parse_error(line_no, line, "ship needs X Y");
      s_scene.ship_set[seat] = true;
      s_scene.ship_x[seat] = x;
      s_scene.ship_y[seat] = y;
      float a;
      if (in >> a) {
        s_scene.ship_angle_set[seat] = true;
        s_scene.ship_angle[seat] = a;
      }
    } else if (cmd == "asteroid") {
      SceneAsteroid a;
      if (!(in >> a.x >> a.y))
        return parse_error(line_no, line, "asteroid needs X Y");
      std::string t;
      while (in >> t) {
        if      (t == "normal")      {}
        else if (t == "invincible")  a.inv   = true;
        else if (t == "invisible")   a.invis = true;
        else if (t == "reflective")  a.refl  = true;
        else if (t == "teleporting") a.tele  = true;
        else if (t == "quantum")     a.quant = true;
        else if (t == "tough")       a.tough = true;
        else if (t == "armoured")    a.arm   = true;
        else if (t == "phasing")     a.phas  = true;
        else if (t.compare(0, 2, "r=") == 0) {
          a.r = (float)atof(t.c_str() + 2);  a.has_r = true;
        } else if (t.compare(0, 2, "v=") == 0) {
          if (sscanf(t.c_str() + 2, "%f,%f", &a.vx, &a.vy) != 2)
            return parse_error(line_no, line, "bad v=VX,VY");
          a.has_v = true;
        } else {
          return parse_error(line_no, line, "unknown asteroid flag");
        }
      }
      s_scene.asteroids.push_back(a);
    } else if (cmd == "enemy") {
      SceneEnemy e;
      if (!(in >> e.x >> e.y))
        return parse_error(line_no, line, "enemy needs X Y");
      in >> e.difficulty;
      s_scene.enemies.push_back(e);
    } else if (cmd == "hazard") {
      std::string kind;
      SceneHazard h;
      if (!(in >> kind >> h.x >> h.y))
        return parse_error(line_no, line, "hazard needs KIND X Y");
      if      (kind == "pulsar") h.kind = Hazard::PULSAR;
      else if (kind == "comet")  h.kind = Hazard::COMET;
      else if (kind == "seeker") h.kind = Hazard::SEEKER;
      else return parse_error(line_no, line, "hazard kind: pulsar|comet|seeker");
      std::string t;
      while (in >> t) {
        if (t.compare(0, 2, "v=") == 0 &&
            sscanf(t.c_str() + 2, "%f,%f", &h.vx, &h.vy) == 2)
          h.has_v = true;
        else
          return parse_error(line_no, line, "unknown hazard flag (v=VX,VY)");
      }
      s_scene.hazards.push_back(h);
    } else if (cmd == "blackhole") {
      SceneBlackHole b;
      if (!(in >> b.x >> b.y))
        return parse_error(line_no, line, "blackhole needs X Y");
      s_scene.black_holes.push_back(b);
    } else if (cmd == "pickup") {
      ScenePickup p;
      if (!(in >> p.type >> p.x >> p.y))
        return parse_error(line_no, line, "pickup needs TYPE X Y");
      s_scene.pickups.push_back(p);
    } else if (cmd == "text") {
      SceneText t;
      if (!(in >> t.x >> t.y >> t.size))
        return parse_error(line_no, line, "text needs X Y SIZE WORDS...");
      std::string rest;
      std::getline(in, rest);
      size_t start = rest.find_first_not_of(" \t");
      if (start == std::string::npos)
        return parse_error(line_no, line, "text needs some words");
      t.text = rest.substr(start);
      // The Typer segment font is uppercase-only, like every menu.
      for (size_t i = 0; i < t.text.size(); i++)
        t.text[i] = (char)std::toupper((unsigned char)t.text[i]);
      s_scene.texts.push_back(t);
    } else if (cmd == "tap") {
      SceneTap t;
      if (!(in >> t.nx >> t.ny))
        return parse_error(line_no, line, "tap needs NX NY (0..1)");
      if (!(in >> t.at_ms)) { t.at_ms = next_key_ms; }
      next_key_ms = t.at_ms + 400;
      s_scene.taps.push_back(t);
    } else if (cmd == "key" || cmd == "hold") {
      std::string name;
      if (!(in >> name)) return parse_error(line_no, line, "key needs a name");
      SceneKey k;
      k.key = key_from_name(name);
      k.hold = (cmd == "hold");
      if (k.key == 0) return parse_error(line_no, line, "unknown key name");
      if (!(in >> k.at_ms)) { k.at_ms = next_key_ms; }
      next_key_ms = k.at_ms + 400;
      s_scene.keys.push_back(k);
    } else {
      return parse_error(line_no, line, "unknown command");
    }
  }
  return true;
}

// On Windows SDL_setenv writes the Win32 environment block, which the
// CRT's std::getenv — a startup snapshot — never sees. The game reads its
// dev flags through BOTH getters (is_beta_feature_enabled() uses
// std::getenv, the replay/START_GENERATION reads use SDL_getenv), so a
// scene's `game N` silently stayed at generation 0 on Windows until the
// flag was set in both worlds.
void set_env_both(const char *k, const char *v) {
  SDL_setenv(k, v, 1);
#ifdef _WIN32
  _putenv_s(k, v);
#else
  setenv(k, v, 1);
#endif
}

Pickup *make_scene_pickup(const std::string &type, const WrappedPoint &at) {
  if (type == "weapon")  return new WeaponPickup(at, 1);
  if (type == "mine")    return new MinePickup(at);
  if (type == "giga")    return new GigaMinePickup(at);
  if (type == "missile") return new MissilePickup(at);
  if (type == "shield")  return new ShieldPickup(at);
  if (type == "god")     return new GodModePickup(at);
  if (type == "nova")    return new NovaChargePickup(at);
  if (type == "beam")    return new BeamPickup(at);
  if (type == "lance")   return new LancePickup(at);
  if (type == "shock")   return new ShockPickup(at);
  if (type == "revive")  return new RevivePickup(at);
  if (type == "life")    return new ExtraLife(at);
  if (type == "timeslow") return new TimeSlowPickup(at);
  return NULL;
}

// ---- PNG writer (8-bit RGB, stored-deflate zlib stream — no deps) ----

void put_be32(std::vector<unsigned char> &v, uint32_t x) {
  v.push_back((unsigned char)(x >> 24));
  v.push_back((unsigned char)(x >> 16));
  v.push_back((unsigned char)(x >> 8));
  v.push_back((unsigned char)x);
}

uint32_t crc32_of(const unsigned char *data, size_t len, uint32_t crc = 0) {
  static uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++)
        c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    init = true;
  }
  crc = crc ^ 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++)
    crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

void png_chunk(std::vector<unsigned char> &out, const char type[4],
               const std::vector<unsigned char> &data) {
  put_be32(out, (uint32_t)data.size());
  size_t type_at = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  put_be32(out, crc32_of(&out[type_at], 4 + data.size()));
}

bool write_png(const std::string &path, const unsigned char *px,
               int w, int h, int channels = 3) {
  // Raw scanlines: filter byte 0 + RGB/RGBA row.
  std::vector<unsigned char> raw;
  raw.reserve((size_t)h * (w * channels + 1));
  for (int y = 0; y < h; y++) {
    raw.push_back(0);
    raw.insert(raw.end(), px + (size_t)y * w * channels,
               px + (size_t)(y + 1) * w * channels);
  }
  // zlib stream: header + stored deflate blocks + adler32.
  std::vector<unsigned char> z;
  z.push_back(0x78);  z.push_back(0x01);
  size_t pos = 0;
  while (pos < raw.size() || raw.empty()) {
    size_t n = raw.size() - pos;
    if (n > 65535) n = 65535;
    bool last = (pos + n == raw.size());
    z.push_back(last ? 1 : 0);
    z.push_back((unsigned char)(n & 0xFF));
    z.push_back((unsigned char)(n >> 8));
    z.push_back((unsigned char)(~n & 0xFF));
    z.push_back((unsigned char)((~n >> 8) & 0xFF));
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    pos += n;
    if (last) break;
  }
  uint32_t a = 1, b = 0;
  for (size_t i = 0; i < raw.size(); i++) {
    a = (a + raw[i]) % 65521;
    b = (b + a) % 65521;
  }
  put_be32(z, (b << 16) | a);

  std::vector<unsigned char> out;
  static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  out.insert(out.end(), sig, sig + 8);
  std::vector<unsigned char> ihdr;
  put_be32(ihdr, (uint32_t)w);
  put_be32(ihdr, (uint32_t)h);
  ihdr.push_back(8);                          // bit depth
  ihdr.push_back(channels == 4 ? 6 : 2);      // truecolour (+alpha)
  ihdr.push_back(0);  ihdr.push_back(0);  ihdr.push_back(0);
  png_chunk(out, "IHDR", ihdr);
  png_chunk(out, "IDAT", z);
  png_chunk(out, "IEND", std::vector<unsigned char>());

  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return false;
  bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
  return fclose(f) == 0 && ok;
}

bool write_bmp(const std::string &path, const unsigned char *rgb,
               int w, int h) {
  // Through SDL so we add no format code: wrap the RGB pixels in a surface.
  SDL_Surface *s = SDL_CreateRGBSurfaceFrom(
      (void *)rgb, w, h, 24, w * 3, 0x0000FF, 0x00FF00, 0xFF0000, 0);
  if (!s) return false;
  bool ok = SDL_SaveBMP(s, path.c_str()) == 0;
  SDL_FreeSurface(s);
  return ok;
}

}  // namespace

bool ShotScene::requested() {
  const char *p = SDL_getenv("NEWTONIA_SHOT");
  return p != NULL && p[0] != '\0';
}

bool ShotScene::init() {
  s_out_path = SDL_getenv("NEWTONIA_SHOT");
  const char *scene = SDL_getenv("NEWTONIA_SHOT_SCENE");
  if (scene && scene[0] && !parse_scene_file(scene)) return false;
  // Env overrides beat the scene file, so one script can be rendered at
  // several sizes/timings without editing it.
  const char *size = SDL_getenv("NEWTONIA_SHOT_SIZE");
  if (size && size[0]) {
    int w = 0, h = 0;
    if (sscanf(size, "%dx%d", &w, &h) != 2 || w < 64 || h < 64) {
      std::cout << "shot: bad NEWTONIA_SHOT_SIZE (want WxH): " << size
                << std::endl;
      return false;
    }
    s_scene.width = w;  s_scene.height = h;
  }
  const char *ms = SDL_getenv("NEWTONIA_SHOT_MS");
  if (ms && ms[0]) s_scene.sim_ms = atoi(ms);
  std::cout << "shot: rendering " << (s_scene.menu_mode ? "menu" : "game")
            << " scene, sim " << s_scene.sim_ms << " ms -> " << s_out_path
            << std::endl;
  return true;
}

int ShotScene::width()  { return s_scene.width; }
int ShotScene::height() { return s_scene.height; }
int ShotScene::sim_ms() { return s_scene.sim_ms; }

State *ShotScene::build_state() {
  // Same seed, same shot: asteroid shapes, starfield, spawn spots.
  srand(s_scene.seed);
  // Starfield density override — shot mode never saves preferences, so
  // poking the live pref is scene-scoped by construction. Set before the
  // state constructors read star_density_scale().
  if (s_scene.star_density >= 0.0f) g_prefs.star_density = s_scene.star_density;
  if (s_scene.menu_mode) return new Menu();

  // A shot game must never touch real player data (see shot_scene.h).
  set_env_both("NEWTONIA_REPLAY_DISABLE", "1");
  if (s_scene.generation > 0) {
    set_env_both("NEWTONIA_BETA", "1");
    char gen[16];
    snprintf(gen, sizeof(gen), "%d", s_scene.generation);
    set_env_both("NEWTONIA_START_GENERATION", gen);
  }
  GLGame *g = new GLGame(PAD_NONE,
                         /*allow_dev_players=*/false);  // scenes own the roster
  g->score_saved = true;    // save_progress() no-ops for the whole run
  g->save_deleted_ = true;  // the game-over savegame delete never fires
  Achievements::note_cheat_used();  // stats/achievements stay cold
  g->shot_hide_hud_ = !s_scene.hud;

  // with_keys binds each seat's PlayerKeys slot, so scene `hold`/`key`
  // events reach player 2 through the standard P2 bindings (i/j/l, '/').
  // bypass_cap: composed 3-4P shots must work while the dark-launch gate
  // (LOCAL_PLAYER_CAP) still holds; the harness is sandboxed anyway.
  for (int i = 1; i < s_scene.num_players; i++)
    g->add_local_player(PAD_NONE, /*with_keys=*/true, /*bypass_cap=*/true);

  // Ships start alive and settled (a fresh game's player 1 opens dead in
  // the respawn countdown), with the camera snapped for determinism.
  // `camera` command state — static so the pref pointer the ships keep
  // outlives this call (an in-game toggle would write through it).
  static bool s_cam_rotate;
  s_cam_rotate = s_scene.camera_rotate;
  for (GLShip *gs : *g->players) {
    if (s_scene.no_ship) {
      // Pure-scenery shots (title cards, capsules): a fresh ship starts
      // dead in the respawn countdown — park it there for the whole sim
      // so nothing is ever drawn. The camera still sits on its position.
      gs->ship->time_until_respawn = 1 << 30;
    } else {
      gs->ship->respawn(g->grid, false);
      gs->ship->bullets.clear();  // no lethal spawn-flash debris
      gs->ship->time_left_invincible = 0;  // no spawn-shield ring in shots
    }
    gs->set_camera_smoothing(0);
    gs->set_rotate_view_pref(&s_cam_rotate);
    // The zoom prefs are neutralized like smoothing above: a maintainer's
    // INI zoom (or speed-follow) must never leak into a committed render —
    // scenes frame with the `zoom` command's explicit FOV below.
    gs->set_zoom_prefs(NULL, NULL);
    if (s_scene.zoom > 0) gs->set_view_angle(s_scene.zoom);
  }

  // Scene coordinates are offsets from player 1's spawn — the camera
  // centre — so compositions read in screen terms (±~900 units vertically
  // at the default zoom).
  // A ship's pointing is its `facing` vector; heading() is derived with
  // 0 deg = up, positive counter-clockwise. Scene angles use the same
  // convention.
  auto facing_from_deg = [](float deg) {
    float rad = (deg + 90.0f) * (float)M_PI / 180.0f;
    return Point(cosf(rad), sinf(rad));
  };
  Ship *p1 = g->players->front()->ship;
  const float ox = p1->position.x(), oy = p1->position.y();
  {
    int seat = 0;
    for (GLShip *gs : *g->players) {
      if (seat >= MAX_PLAYERS) break;
      if (s_scene.ship_set[seat])
        gs->ship->position = WrappedPoint(ox + s_scene.ship_x[seat],
                                          oy + s_scene.ship_y[seat]);
      if (s_scene.ship_angle_set[seat])
        gs->ship->facing = facing_from_deg(s_scene.ship_angle[seat]);
      seat++;
    }
  }

  // Scene `ship`/`ship2` placement overrides respawn's safe_position, so a
  // busy generation could drop a ship straight onto a rock (or right next
  // to one it then thrusts into). Sweep generation-spawned asteroids clear
  // of each player — runs BEFORE the composed spawns, which are placed on
  // purpose.
  for (GLShip *gs : *g->players) {
    auto it = g->objects->begin();
    while (it != g->objects->end()) {
      float clearance = (*it)->effective_radius() + 300.0f;
      if ((*it)->position.distance_to(gs->ship->position) < clearance) {
        delete *it;
        it = g->objects->erase(it);
      } else {
        ++it;
      }
    }
  }

  if (s_scene.clear_world) {
    for (Asteroid *a : *g->objects) delete a;
    g->objects->clear();
    for (Hazard *h : *g->hazards) delete h;
    g->hazards->clear();
    for (Pickup *p : *g->pickups) delete p;
    g->pickups->clear();
    for (BlackHole *b : *g->black_holes) delete b;
    g->black_holes->clear();
  }

  // Composed enemies get the same clearance sweep as the players — a dense
  // late generation otherwise drops them onto rocks. Runs BEFORE the
  // composed asteroid spawns so it can only cull generation rocks, never
  // scene-placed ones.
  for (const SceneEnemy &se : s_scene.enemies) {
    WrappedPoint at(ox + se.x, oy + se.y);
    auto it = g->objects->begin();
    while (it != g->objects->end()) {
      if ((*it)->position.distance_to(at) <
          (*it)->effective_radius() + 250.0f) {
        delete *it;
        it = g->objects->erase(it);
      } else {
        ++it;
      }
    }
  }

  for (const SceneAsteroid &sa : s_scene.asteroids) {
    Asteroid *a = new Asteroid(sa.inv, sa.invis, sa.refl, sa.tele, sa.quant,
                               sa.tough, sa.arm, sa.phas);
    a->position = WrappedPoint(ox + sa.x, oy + sa.y);
    if (sa.has_r) {
      float r = sa.r;
      if (r > Asteroid::max_radius) {
        // The collision grid's cell size assumes max_radius.
        std::cout << "shot: asteroid r=" << r << " clamped to "
                  << Asteroid::max_radius << std::endl;
        r = (float)Asteroid::max_radius;
      }
      a->radius = r;
      a->radius_squared = r * r;
    }
    // Scene velocities are units/second; Object::step integrates per ms.
    if (sa.has_v) a->velocity = Point(sa.vx / 1000.0f, sa.vy / 1000.0f);
    g->objects->push_back(a);
  }
  // The sweeps/clear above deleted asteroids the grid still points at, and
  // the enemy constructor's safe_position consults the grid — refresh it
  // before anything walks it.
  g->grid.update((std::list<Object *> *)g->objects);
  for (const SceneEnemy &se : s_scene.enemies) {
    GLEnemy *e = new GLEnemy(g->grid, ox + se.x, oy + se.y, g->players,
                             se.difficulty, (std::list<Object *> *)g->objects);
    // A bare Ship starts dead awaiting respawn; wake it the way the
    // station's restore path does, and skip the Follower's initial lock
    // delay so it engages within the shot's sim window.
    e->ship->alive = true;
    e->ship->position = WrappedPoint(ox + se.x, oy + se.y);
    if (!e->ship->behaviours.empty())
      if (Follower *f = dynamic_cast<Follower *>(e->ship->behaviours.front()))
        f->lock_now();
    g->enemies->push_back(e);
    g->ship_objects->push_back(e->ship);
  }
  for (const SceneHazard &sh : s_scene.hazards) {
    Hazard *hz = new Hazard(sh.kind, g->world);
    hz->position = WrappedPoint(ox + sh.x, oy + sh.y);
    // Units/second, like the asteroids (a natural comet cruises at ~280).
    if (sh.has_v) hz->velocity = Point(sh.vx / 1000.0f, sh.vy / 1000.0f);
    g->hazards->push_back(hz);
  }
  for (const SceneBlackHole &sb : s_scene.black_holes)
    g->black_holes->push_back(
        new BlackHole(WrappedPoint(ox + sb.x, oy + sb.y)));
  for (const ScenePickup &sp : s_scene.pickups) {
    Pickup *p = make_scene_pickup(sp.type, WrappedPoint(ox + sp.x, oy + sp.y));
    if (!p) {
      std::cout << "shot: unknown pickup type " << sp.type << std::endl;
      delete g;
      return NULL;
    }
    g->pickups->push_back(p);
  }

  g->grid.update((std::list<Object *> *)g->objects);
  // Sentinel: an emptied (or all-invincible) scene must not start the
  // level-clear countdown — a mid-shot world rebuild. Nothing can decrement
  // this when no killable asteroid exists.
  if (Asteroid::num_killable == 0) Asteroid::num_killable = 1;
  return g;
}

void ShotScene::log_state(State *state) {
  GLGame *g = dynamic_cast<GLGame *>(state);
  if (!g) return;
  int i = 0;
  for (GLShip *gs : *g->players) {
    std::cout << "shot: player " << ++i << " alive=" << gs->ship->is_alive()
              << " score=" << gs->ship->score << std::endl;
  }
  std::cout << "shot: world " << g->world.x() << "x" << g->world.y() << ", "
            << g->objects->size() << " asteroids, " << g->hazards->size()
            << " hazards" << std::endl;
  if (!g->enemies->empty()) {
    int alive = 0;
    Ship *p1 = g->players->front()->ship;
    std::string near_txt;
    for (GLShip *e : *g->enemies) {
      if (!e->ship->is_alive()) continue;
      alive++;
      char buf[64];
      snprintf(buf, sizeof(buf), " (%.0f,%.0f)",
               e->ship->position.x() - p1->position.x(),
               e->ship->position.y() - p1->position.y());
      near_txt += buf;
    }
    std::cout << "shot: enemies alive=" << alive << " rel-p1:" << near_txt
              << std::endl;
  }
}

void ShotScene::pump_keys(State *state, int t_ms) {
  for (SceneTap &t : s_scene.taps) {
    if (!t.sent && t_ms >= t.at_ms) {
      t.sent = true;
      state->touch_tap(t.nx, t.ny);
    }
  }
  for (SceneKey &k : s_scene.keys) {
    if (!k.downed && t_ms >= k.at_ms) {
      k.downed = true;
      state->keyboard(k.key, 0, 0);
    }
    // Release a beat later — menus act on key-up, gameplay on held keys.
    // A `hold` never releases: thrust flames and autofire stay live in the
    // captured frame.
    if (!k.hold && k.downed && !k.upped && t_ms >= k.at_ms + 100) {
      k.upped = true;
      state->keyboard_up(k.key, 0, 0);
    }
  }
}

void ShotScene::draw_overlays(int window_w, int window_h) {
  if (s_scene.texts.empty()) return;
  glViewport(0, 0, window_w, window_h);
  float ortho[16];
  mat4_ortho(ortho, (float)-window_w, (float)window_w, (float)-window_h,
             (float)window_h, -1.0f, 1.0f);
  gles2_set_vp(ortho);
  // Typer coordinates are virtual units (multiplied by Typer::scale); a
  // caption's X/Y are window fractions, -1..1 with (0,0) the centre, +y up.
  // draw_centered() centres horizontally but TOP-anchors vertically (glyphs
  // descend 2*size below the given y — see the menu title comments); adding
  // size makes the scene's Y the text's vertical CENTRE, so `text 0 0`
  // lands dead centre.
  for (const SceneText &t : s_scene.texts)
    Typer::draw_centered(t.x * window_w / Typer::scale,
                         t.y * window_h / Typer::scale + t.size,
                         t.text.c_str(), t.size);
}

bool ShotScene::capture(int window_w, int window_h) {
  std::vector<unsigned char> rgba((size_t)window_w * window_h * 4);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, window_w, window_h, GL_RGBA, GL_UNSIGNED_BYTE,
               rgba.data());
  // Flip to top-down rows and drop alpha.
  std::vector<unsigned char> rgb((size_t)window_w * window_h * 3);
  for (int y = 0; y < window_h; y++) {
    const unsigned char *src =
        &rgba[(size_t)(window_h - 1 - y) * window_w * 4];
    unsigned char *dst = &rgb[(size_t)y * window_w * 3];
    for (int x = 0; x < window_w; x++) {
      dst[x * 3 + 0] = src[x * 4 + 0];
      dst[x * 3 + 1] = src[x * 4 + 1];
      dst[x * 3 + 2] = src[x * 4 + 2];
    }
  }
  bool bmp = s_out_path.size() > 4 &&
             s_out_path.compare(s_out_path.size() - 4, 4, ".bmp") == 0;
  bool ok;
  if (s_scene.transparent && !bmp) {
    // Black is the void: alpha = the brightest channel, colour scaled back
    // to full strength (un-premultiplied) so partially-covered stroke
    // pixels composite as translucent green, not dark green.
    std::vector<unsigned char> out((size_t)window_w * window_h * 4);
    for (size_t i = 0, n = (size_t)window_w * window_h; i < n; i++) {
      unsigned char r = rgb[i * 3], g = rgb[i * 3 + 1], b = rgb[i * 3 + 2];
      unsigned char a = r > g ? (r > b ? r : b) : (g > b ? g : b);
      if (a > 0) {
        out[i * 4 + 0] = (unsigned char)((r * 255 + a / 2) / a);
        out[i * 4 + 1] = (unsigned char)((g * 255 + a / 2) / a);
        out[i * 4 + 2] = (unsigned char)((b * 255 + a / 2) / a);
      } else {
        out[i * 4 + 0] = out[i * 4 + 1] = out[i * 4 + 2] = 0;
      }
      out[i * 4 + 3] = a;
    }
    ok = write_png(s_out_path, out.data(), window_w, window_h, 4);
  } else {
    ok = bmp ? write_bmp(s_out_path, rgb.data(), window_w, window_h)
             : write_png(s_out_path, rgb.data(), window_w, window_h);
  }
  std::cout << (ok ? "shot: wrote " : "shot: FAILED writing ") << s_out_path
            << " (" << window_w << "x" << window_h << ", sim "
            << s_scene.sim_ms << " ms)" << std::endl;
  return ok;
}
