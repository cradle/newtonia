#include "ship.h"
#include "achievements.h"
#include "stats.h"
#include "asset_path.h"
#include "sound_cache.h"
#include "point.h"
#include "particle.h"
#include "asteroid.h"
#include "behaviour.h"
#include "weapon/base.h"
#include "weapon/default.h"
#include "weapon/mine.h"
#include "weapon/giga_mine.h"
#include "weapon/missile.h"
#include "weapon/shield.h"
#include "weapon/god_mode.h"
#include "weapon/nova.h"
#include "weapon/beam.h"
#include "weapon/lance.h"
#include "weapon/shock.h"
#include "net_protocol.h"  // NET_LOG

static const int NOVA_MAX_AMMO = 10;

bool Ship::net_quiet_respawn = false;
std::vector<Ship::NetShipImpact> Ship::net_ship_impacts;
std::vector<const Ship*> Ship::net_shots;
std::vector<const Ship*> Ship::net_booms;
std::vector<Ship::NetKillClaim> Ship::net_kill_claims;
std::vector<Ship::NetShotReport> Ship::net_shot_reports;
std::vector<std::vector<Point>> Ship::net_lance_reports;
std::vector<std::vector<Point>> Ship::net_shock_reports;
std::vector<Ship::NetBounceReport> Ship::net_bounce_reports;
bool Ship::net_report_bounces = false;
std::vector<std::pair<const Ship*, uint8_t>> Ship::net_ach_relays;
std::vector<const Ship*> Ship::net_ram_blasts;
std::vector<Ship::NetShipHit> Ship::net_ship_hit_claims;
uint32_t Ship::net_next_ship_id = 0;
#include <algorithm>
#include <math.h>
#include <climits>
#include <iostream>

using namespace std;

Ship::Ship(const Grid &grid, bool has_friction) :
    CompositeObject(),
    toggled(false) {
  alive = false;
  first_life = true;
  score = 0;
  kills = 0;
  nova_charge = 0;
  nova_kill_counter = 0;
  bullet_trails.reserve(256);
  position = WrappedPoint();
  safe_position(grid);
  init(!has_friction);
  if(tic_sound == NULL) {
    tic_sound = load_wav_cached("audio/tic.wav");
    if(tic_sound == NULL) {
      std::cout << "Unable to load tic.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(tic_low_sound == NULL) {
    tic_low_sound = load_wav_cached("audio/tic_low.wav");
    if(tic_low_sound == NULL) {
      std::cout << "Unable to load tic_low.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(click_sound == NULL) {
    click_sound = load_wav_cached("audio/click.wav");
    if(click_sound == NULL) {
      std::cout << "Unable to load click.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(boost_sound == NULL) {
    boost_sound = load_wav_cached("audio/boost.wav");
  }
  if(boost_sound != NULL) {
    Mix_VolumeChunk(boost_sound, 0);
    boost_channel = Mix_PlayChannel(-1, boost_sound, -1);
  } else {
    std::cout << "Unable to load boost.wav (" << Mix_GetError() << ")" << std::endl;
  }
  if(missile_explode_sound == NULL) {
    missile_explode_sound = load_wav_cached("audio/missile_explode.wav");
    if(missile_explode_sound == NULL) {
      std::cout << "Unable to load missile_explode.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(shield_hum_sound == NULL) {
    shield_hum_sound = load_wav_cached("audio/shield_hum.wav");
  }
  if(shield_hum_sound == NULL) {
    std::cout << "Unable to load shield_hum.wav (" << Mix_GetError() << ")" << std::endl;
  }
  if(explode_sound == NULL) {
    explode_sound = load_wav_cached("audio/explode.wav");
    if(explode_sound == NULL) {
      std::cout << "Unable to load explode.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(giga_mine_explode_sound == NULL) {
    giga_mine_explode_sound = load_wav_cached("audio/giga_mine_explode.wav");
    if(giga_mine_explode_sound == NULL) {
      std::cout << "Unable to load giga_mine_explode.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(mine_explode_sound == NULL) {
    mine_explode_sound = load_wav_cached("audio/mine_explode.wav");
    if(mine_explode_sound == NULL) {
      std::cout << "Unable to load mine_explode.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(missile_fly_sound == NULL) {
    missile_fly_sound = load_wav_cached("audio/missile_fly.wav");
    if(missile_fly_sound == NULL) {
      std::cout << "Unable to load missile_fly.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(shoot_sound == NULL) {
    shoot_sound = load_wav_cached("audio/shoot.wav");
    if(shoot_sound == NULL) {
      std::cout << "Unable to load shoot.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(god_mode_music_sound == NULL) {
    god_mode_music_sound = load_wav_cached("audio/god_mode_music.wav");
    if(god_mode_music_sound == NULL) {
      std::cout << "Unable to load god_mode_music.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
  if(god_mode_music_warn_sound == NULL) {
    god_mode_music_warn_sound = load_wav_cached("audio/god_mode_music_warn.wav");
    if(god_mode_music_warn_sound == NULL) {
      std::cout << "Unable to load god_mode_music_warn.wav (" << Mix_GetError() << ")" << std::endl;
    }
  }
}

void Ship::mute_engine() {
  if(boost_channel >= 0) {
    Mix_HaltChannel(boost_channel);
    boost_channel = -1;
  }
}

// Halt every continuous per-ship sound. On a net client the shield hum,
// god-mode music and boost are driven by snapshots; when the host leaves
// snapshots stop, so nothing would ever turn them off — the game freezes
// with the loop stuck on. Called on the client's terminal disconnect.
void Ship::silence_loops() {
  set_shield_hum(false);
  stop_god_mode_music();
  mute_engine();
}

void Ship::add_behaviour(Behaviour *b) {
  behaviours.push_back(b);
}

void Ship::disable_behaviours() {
  while(!behaviours.empty()) {
    delete behaviours.back();
    behaviours.pop_back();
  }
}

void Ship::disable_weapons() {
  while(!primary_weapons.empty()) {
    delete primary_weapons.back();
    primary_weapons.pop_back();
  }
  while(!secondary_weapons.empty()) {
    delete secondary_weapons.back();
    secondary_weapons.pop_back();
  }
}

Ship::~Ship() {
  disable_weapons();
  disable_behaviours();
  if(tic_sound != NULL) {
    Mix_FreeChunk(tic_sound);
  }
  if(tic_low_sound != NULL) {
    Mix_FreeChunk(tic_low_sound);
  }
  if(boost_channel >= 0) {
    Mix_HaltChannel(boost_channel);
  }
  if(boost_sound != NULL) {
    Mix_FreeChunk(boost_sound);
  }
  if(click_sound != NULL) {
    Mix_FreeChunk(click_sound);
  }
  if(missile_explode_sound != NULL) {
    Mix_FreeChunk(missile_explode_sound);
  }
  if(shield_hum_channel >= 0) {
    Mix_HaltChannel(shield_hum_channel);
  }
  if(shield_hum_sound != NULL) {
    Mix_FreeChunk(shield_hum_sound);
  }
  if(explode_sound != NULL) {
    Mix_FreeChunk(explode_sound);
  }
  if(giga_mine_explode_sound != NULL) {
    Mix_FreeChunk(giga_mine_explode_sound);
  }
  if(mine_explode_sound != NULL) {
    Mix_FreeChunk(mine_explode_sound);
  }
  if(missile_fly_sound != NULL) {
    Mix_FreeChunk(missile_fly_sound);
  }
  if(shoot_sound != NULL) {
    Mix_FreeChunk(shoot_sound);
  }
  stop_god_mode_music();
  if(god_mode_music_sound != NULL) {
    Mix_FreeChunk(god_mode_music_sound);
  }
  if(god_mode_music_warn_sound != NULL) {
    Mix_FreeChunk(god_mode_music_warn_sound);
  }
}

// Identity key for a primary weapon (kind + Default-gun variant index),
// used by the exhausted-primary fallback to find "the weapon the player was
// using before this one" across list reshuffles and netplay roster
// rebuilds. index_out is the Default gun's weapon_index, -1 for the rest.
Save::WeaponEntry::Kind Ship::primary_kind_of(Weapon::Base *w, int *index_out) {
  *index_out = -1;
  if(dynamic_cast<Weapon::GodMode*>(w)) return Save::WeaponEntry::Kind::GodMode;
  if(dynamic_cast<Weapon::Beam*>(w))    return Save::WeaponEntry::Kind::Beam;
  if(dynamic_cast<Weapon::Lance*>(w))   return Save::WeaponEntry::Kind::Lance;
  if(dynamic_cast<Weapon::Shock*>(w))   return Save::WeaponEntry::Kind::Shock;
  Weapon::Default *d = dynamic_cast<Weapon::Default*>(w);
  if(d) *index_out = d->weapon_index();
  return Save::WeaponEntry::Kind::Default;
}

// Remember the CURRENT primary as "the weapon used before" — every path
// that moves the selection (cycling, pickup auto-select) calls this first,
// so an exhausted weapon can hand the trigger back to it (see shoot()).
void Ship::record_primary_selection() {
  if(primary_weapons.empty() || primary == primary_weapons.end()) return;
  last_primary_kind = primary_kind_of(*primary, &last_primary_index);
  has_last_primary = true;
}

// Where the selection lands after removing a primary. Hand the trigger back
// to the weapon the player was using BEFORE this one (selection history,
// kind+variant identity). Fall back to the previous list neighbour — pickups
// splice the selection to the back, so the neighbour is the most recent
// pickup — and only wrap forward when removing the front. The old
// wrap-forward-only rule landed on the default gun every time, because the
// spent weapon was always last. Shared by the exhausted-primary removal in
// shoot() and the god-mode expiry in step().
list<Weapon::Base *>::iterator Ship::fallback_primary(list<Weapon::Base *>::iterator to_remove) {
  if(has_last_primary) {
    for(auto it = primary_weapons.begin(); it != primary_weapons.end(); ++it) {
      if(it == to_remove) continue;
      int idx;
      if(primary_kind_of(*it, &idx) == last_primary_kind &&
         idx == last_primary_index) {
        return it;
      }
    }
  }
  auto next = to_remove;
  if(next != primary_weapons.begin()) --next;
  else ++next;  // removing the front: the only direction left
  return next;
}

void Ship::next_weapon() {
  net_next_weapon_count++;
  if(god_mode_time_remaining() > 0) return;
  if(click_sound != NULL) {
    Mix_PlayChannel(-1, click_sound, 0);
  }
  list<Weapon::Base *>::iterator next(primary);

  //TODO: delete exhausted
  //TODO: use circular list?
  next++;

  if(next == primary_weapons.end())
    next = primary_weapons.begin();

  if(next != primary) record_primary_selection();
  (*next)->shoot((*primary)->is_shooting());
  (*primary)->shoot(false);

  primary = next;
}

void Ship::previous_weapon() {
  if(god_mode_time_remaining() > 0) return;
  if(click_sound != NULL) {
    Mix_PlayChannel(-1, click_sound, 0);
  }
  list<Weapon::Base *>::iterator next(primary);

  //TODO: use circular list?
  if(next == primary_weapons.begin())
    next = primary_weapons.end();

  next--;

  if(next != primary) record_primary_selection();
  (*next)->shoot((*primary)->is_shooting());
  (*primary)->shoot(false);
  primary = next;
}

void Ship::next_secondary_weapon() {
  net_next_secondary_count++;
  if(secondary_weapons.empty()) return;
  if(click_sound != NULL) {
    Mix_PlayChannel(-1, click_sound, 0);
  }
  list<Weapon::Base *>::iterator next(secondary);
  next++;
  if(next == secondary_weapons.end())
    next = secondary_weapons.begin();
  (*secondary)->shoot(false);
  secondary = next;
}

struct WeaponConfig {
  bool automatic;
  int level;
  float accuracy;
  int time_between_shots;
};

static const WeaponConfig weapon_configs[] = {
  { false, 1, 0.1f, 100 },   // 0
  { false, 2, 0.1f, 100 },   // 1
  { false, 3, 0.1f, 100 },   // 2
  { false, 4, 0.1f, 100 },   // 3
  { false, 5, 0.1f, 100 },   // 4
  { false, 0, 0.1f,  50 },   // 5
  { false, 0, 0.0f, 200 },   // 6
  { true,  0, 0.1f, 100 },   // 7
  { true,  1, 0.1f, 100 },   // 8
  { true,  2, 0.1f, 100 },   // 9
  { true,  3, 0.1f, 100 },   // 10
  { true,  4, 0.1f, 100 },   // 11
  { true,  5, 0.1f, 100 },   // 12
  { true,  0, 0.1f,  50 },   // 13
  { true,  0, 0.0f, 200 },   // 14
};

static const int num_weapon_configs = sizeof(weapon_configs) / sizeof(weapon_configs[0]);

void Ship::add_weapon(int weapon_index) {
  if(weapon_index < 0 || weapon_index >= num_weapon_configs) return;
  const WeaponConfig &cfg = weapon_configs[weapon_index];

  bool in_god_mode = god_mode_time_remaining() > 0;
  bool auto_shooting = !primary_weapons.empty() && (*primary)->is_automatic() && (*primary)->is_shooting();

  for(auto it = primary_weapons.begin(); it != primary_weapons.end(); ++it) {
    Weapon::Default *w = dynamic_cast<Weapon::Default*>(*it);
    if(w && w->weapon_index() == weapon_index) {
      w->add_ammo(100);
      if(!in_god_mode && !auto_shooting) {
        if(it != primary) record_primary_selection();
        (*primary)->shoot(false);
        primary_weapons.splice(primary_weapons.end(), primary_weapons, it);
        primary = --primary_weapons.end();
      }
      return;
    }
  }

  primary_weapons.push_back(new Weapon::Default(this, cfg.automatic, cfg.level, cfg.accuracy, cfg.time_between_shots, weapon_index));
  if(!in_god_mode && !auto_shooting) {
    if (primary != primary_weapons.end()) {
      record_primary_selection();
      (*primary)->shoot(false);
    }
    primary = --primary_weapons.end();
  }
}

// Returns true if the player is currently holding the shield key.
// In that case we add ammo to a picked-up secondary without switching away.
static bool shield_held(const list<Weapon::Base*>& secondary_weapons,
                        list<Weapon::Base*>::const_iterator secondary) {
  if (secondary == secondary_weapons.end()) return false;
  return dynamic_cast<const Weapon::Shield*>(*secondary) && (*secondary)->is_shooting();
}

void Ship::add_mine_ammo(int amount) {
  for(auto it = secondary_weapons.begin(); it != secondary_weapons.end(); ++it) {
    if(dynamic_cast<Weapon::Mine*>(*it)) {
      (*it)->add_ammo(amount);
      if(!shield_held(secondary_weapons, secondary)) {
        (*secondary)->shoot(false);
        secondary = it;
      }
      return;
    }
  }
  Weapon::Mine *w = new Weapon::Mine(this);
  w->set_ammo(amount);
  secondary_weapons.push_back(w);
  if(!shield_held(secondary_weapons, secondary))
    secondary = --secondary_weapons.end();
}

void Ship::add_giga_mine_ammo(int amount) {
  for(auto it = secondary_weapons.begin(); it != secondary_weapons.end(); ++it) {
    if(dynamic_cast<Weapon::GigaMine*>(*it)) {
      (*it)->add_ammo(amount);
      if(!shield_held(secondary_weapons, secondary)) {
        (*secondary)->shoot(false);
        secondary = it;
      }
      return;
    }
  }
  Weapon::GigaMine *w = new Weapon::GigaMine(this);
  w->set_ammo(amount);
  secondary_weapons.push_back(w);
  if(!shield_held(secondary_weapons, secondary))
    secondary = --secondary_weapons.end();
}

void Ship::add_missile_ammo(int amount) {
  // Find the Missile weapon in secondary_weapons, add ammo and switch to it.
  for(auto it = secondary_weapons.begin(); it != secondary_weapons.end(); ++it) {
    if(dynamic_cast<Weapon::Missile*>(*it)) {
      (*it)->add_ammo(amount);
      if(!shield_held(secondary_weapons, secondary)) {
        (*secondary)->shoot(false);
        secondary = it;
      }
      return;
    }
  }
  Weapon::Missile *w = new Weapon::Missile(this);
  w->set_ammo(amount);
  if(missile_asteroids) w->set_asteroids(missile_asteroids);
  if(missile_ships_list) w->set_ship_targets(missile_ships_list);
  secondary_weapons.push_back(w);
  if(!shield_held(secondary_weapons, secondary))
    secondary = --secondary_weapons.end();
}

void Ship::add_shield_ammo(int amount) {
  for(auto it = secondary_weapons.begin(); it != secondary_weapons.end(); ++it) {
    if(dynamic_cast<Weapon::Shield*>(*it)) {
      (*it)->add_ammo(amount);
      // Only re-select the shield if it isn't already the active held weapon,
      // to avoid calling shoot(false) and briefly dropping the shield.
      if(secondary != it) {
        (*secondary)->shoot(false);
        secondary = it;
      }
      return;
    }
  }
  Weapon::Shield *w = new Weapon::Shield(this);
  w->set_ammo(amount);
  secondary_weapons.push_back(w);
  secondary = --secondary_weapons.end();
}

void Ship::add_beam_ammo(int amount) {
  // Primary weapon, like a picked-up Default gun: reuse an existing Beam and
  // switch to it, or create one and select it (unless god mode / an automatic
  // gun is currently being held, matching add_weapon()).
  bool in_god_mode = god_mode_time_remaining() > 0;
  bool auto_shooting = !primary_weapons.empty() && (*primary)->is_automatic() && (*primary)->is_shooting();

  for(auto it = primary_weapons.begin(); it != primary_weapons.end(); ++it) {
    if(dynamic_cast<Weapon::Beam*>(*it)) {
      (*it)->add_ammo(amount);
      if(!in_god_mode && !auto_shooting) {
        if(it != primary) record_primary_selection();
        (*primary)->shoot(false);
        primary_weapons.splice(primary_weapons.end(), primary_weapons, it);
        primary = --primary_weapons.end();
      }
      return;
    }
  }

  Weapon::Beam *w = new Weapon::Beam(this);
  w->add_ammo(amount);
  primary_weapons.push_back(w);
  if(!in_god_mode && !auto_shooting) {
    if (primary != primary_weapons.end()) {
      record_primary_selection();
      (*primary)->shoot(false);
    }
    primary = --primary_weapons.end();
  }
}

void Ship::add_lance_ammo(int amount) {
  // Primary weapon; same reuse-or-create-and-select logic as add_beam_ammo().
  bool in_god_mode = god_mode_time_remaining() > 0;
  bool auto_shooting = !primary_weapons.empty() && (*primary)->is_automatic() && (*primary)->is_shooting();

  for(auto it = primary_weapons.begin(); it != primary_weapons.end(); ++it) {
    if(dynamic_cast<Weapon::Lance*>(*it)) {
      (*it)->add_ammo(amount);
      if(!in_god_mode && !auto_shooting) {
        if(it != primary) record_primary_selection();
        (*primary)->shoot(false);
        primary_weapons.splice(primary_weapons.end(), primary_weapons, it);
        primary = --primary_weapons.end();
      }
      return;
    }
  }

  Weapon::Lance *w = new Weapon::Lance(this);
  w->add_ammo(amount);
  primary_weapons.push_back(w);
  if(!in_god_mode && !auto_shooting) {
    if (primary != primary_weapons.end()) {
      record_primary_selection();
      (*primary)->shoot(false);
    }
    primary = --primary_weapons.end();
  }
}

// Shock is a primary weapon (lives in primary_weapons, fired via shoot()).
// Selection mirrors add_weapon(): switch to it on pickup unless in god mode or
// mid-auto-fire, so it doesn't yank the gun out from under a held trigger.
void Ship::add_shock(int amount) {
  bool in_god_mode = god_mode_time_remaining() > 0;
  bool auto_shooting = !primary_weapons.empty() && (*primary)->is_automatic() && (*primary)->is_shooting();

  for(auto it = primary_weapons.begin(); it != primary_weapons.end(); ++it) {
    if(dynamic_cast<Weapon::Shock*>(*it)) {
      (*it)->add_ammo(amount);
      if(!in_god_mode && !auto_shooting) {
        if(it != primary) record_primary_selection();
        (*primary)->shoot(false);
        primary_weapons.splice(primary_weapons.end(), primary_weapons, it);
        primary = --primary_weapons.end();
      }
      return;
    }
  }
  Weapon::Shock *w = new Weapon::Shock(this);
  w->set_ammo(amount);
  primary_weapons.push_back(w);
  if(!in_god_mode && !auto_shooting) {
    if(primary != primary_weapons.end()) {
      record_primary_selection();
      (*primary)->shoot(false);
    }
    primary = --primary_weapons.end();
  }
}

void Ship::give_all_weapons(int ammo) {
  // Every primary gun variant, plus the shock/beam/lance primaries.
  for (int i = 0; i < num_weapon_configs; i++) add_weapon(i);
  add_beam_ammo(ammo);
  add_lance_ammo(ammo);
  add_shock(ammo);
  // Every secondary. Nova clamps to its own cap inside add_nova_ammo().
  add_mine_ammo(ammo);
  add_giga_mine_ammo(ammo);
  add_missile_ammo(ammo);
  add_shield_ammo(ammo);
  add_nova_ammo(ammo);
  // Top every limited-ammo weapon up to `ammo` (base gun stays unlimited; Nova
  // keeps its design cap set above).
  for (auto *w : primary_weapons)
    if (!w->is_unlimited()) w->set_ammo(ammo);
  for (auto *w : secondary_weapons)
    if (!dynamic_cast<Weapon::Nova*>(w)) w->set_ammo(ammo);
  // Each add_* selects the weapon it just stocked, which leaves Nova (added
  // last) on the trigger; test sessions want the everyday Mine armed.
  if (!shield_held(secondary_weapons, secondary)) {
    for (auto it = secondary_weapons.begin(); it != secondary_weapons.end(); ++it) {
      if (dynamic_cast<Weapon::Mine*>(*it)) {
        (*secondary)->shoot(false);
        secondary = it;
        break;
      }
    }
  }
}

void Ship::add_god_mode(int duration_ms) {
  // God mode starts firing shockwaves the moment it activates, so activation
  // counts as usage for the weapons_7 achievement.
  record_weapon_fired(Save::WeaponEntry::Kind::GodMode);
  set_shield_hum(false);
  for(auto it = primary_weapons.begin(); it != primary_weapons.end(); ++it) {
    if(dynamic_cast<Weapon::GodMode*>(*it)) {
      (*it)->set_ammo(duration_ms);
      invincible = true;
      god_mode_music_phase = 0;  // force phase re-evaluation so warn→main on re-pickup
      update_god_mode_music(duration_ms);
      return;
    }
  }
  primary_weapons.push_back(new Weapon::GodMode(this, duration_ms));
  if (primary != primary_weapons.end()) {
    record_primary_selection();
    (*primary)->shoot(false);
  }
  primary = --primary_weapons.end();
  update_god_mode_music(duration_ms);
}

void Ship::add_nova_charge(int n) {
  nova_charge += n;
  if (nova_charge >= NOVA_MAX_AMMO) {
    nova_charge = 0;
    add_nova_ammo(1);
  }
}

void Ship::tally_nova_kill(const Point &pos) {
  nova_kill_counter++;
  if (nova_kill_counter >= 100) {
    nova_kill_counter = 0;
    nova_drops_pending.push_back(pos);
  }
}

// Nova shockwaves pass nova_feedback=false so a nova's own kills don't feed
// the next nova charge. Achievements and lifetime stats accrue only to
// locally-controlled players (ACHIEVEMENTS.md §4) — enemies and the
// mini-station reuse these collision paths but earn nothing.
void Ship::credit_asteroid_kill(Object *object, bool nova_feedback) {
  kills_this_life += 1;
  kills += 1;
  asteroid_kills += 1;
  if (nova_feedback) tally_nova_kill(object->position);
  if (!is_local_player) return;

  // Lifetime stats are frozen during cheated games too, not just the
  // unlock/progress calls: specials_7 and kills_10000_lifetime read
  // stats.dat, so a dev-start run would otherwise bank progress there for a
  // later clean game to cash in — the same laundering the game-scoped cheat
  // flag exists to prevent.
  if (!Achievements::unlocks_suppressed()) {
    Stats::add_kill();
    Asteroid *ast = dynamic_cast<Asteroid*>(object);
    if (ast) {
      if (ast->teleporting) Stats::note_special_kill(Stats::SPECIAL_TELEPORTING);
      if (ast->invisible)   Stats::note_special_kill(Stats::SPECIAL_INVISIBLE);
      if (ast->quantum)     Stats::note_special_kill(Stats::SPECIAL_QUANTUM);
      if (ast->tough)       Stats::note_special_kill(Stats::SPECIAL_TOUGH);
      if (ast->armoured)    Stats::note_special_kill(Stats::SPECIAL_ARMOURED);
      if (ast->phasing)     Stats::note_special_kill(Stats::SPECIAL_PHASING);
      // Plain asteroids count as the seventh type; reflective/invincible are
      // god-mode-only bonus kills and set no bit.
      if (!ast->teleporting && !ast->invisible && !ast->quantum && !ast->tough &&
          !ast->armoured && !ast->phasing && !ast->reflective && !ast->invincible)
        Stats::note_special_kill(Stats::SPECIAL_NORMAL);
    }
  }

  if (asteroid_kills == 1) Achievements::unlock("first_kill");
  Achievements::progress("kills_1000", asteroid_kills / 10);  // 1,000 kills == 100%
  Achievements::progress("kills_10000_lifetime", (int)(Stats::lifetime_kills() / 100));
  Achievements::progress("score_3m", score / 30000);  // 3,000,000 points == 100%
  // Frozen target (ACHIEVEMENTS.md §5 future-proofing rule): any 7 distinct
  // special types unlock, counted across the whole mask so a future 8th type
  // widens the pool without raising the bar. New types get a NEW achievement.
  static const int SPECIALS_TARGET = 7;
  int specials = 0;
  for (int i = 0; i < 32; i++)
    if (Stats::special_kill_mask() & (1u << i)) specials++;
  if (specials > SPECIALS_TARGET) specials = SPECIALS_TARGET;
  Achievements::progress("specials_7", specials * 100 / SPECIALS_TARGET);
}

// Bookkeeping for a ship this ship shot down (bullet and missile paths).
// The enemies_10 achievement counts AI ships only: another local player
// (friendly fire) doesn't qualify, and a future remote netplay peer is a
// player too, not an enemy.
void Ship::credit_ship_kill(Ship *other) {
  kills_this_life += 1;
  kills += 1;
  score += other->value * multiplier();
  if (!is_local_player) return;
  Achievements::progress("score_3m", score / 30000);
  if (other->is_local_player) return;  // friendly fire: no enemy credit
  enemy_kills += 1;
  Achievements::progress("enemies_10", enemy_kills * 10);  // 10 ships == 100%
}

// WeaponEntry::Kind values double as bit positions in weapons_fired_mask.
void Ship::record_weapon_fired(Save::WeaponEntry::Kind kind) {
  weapons_fired_mask |= 1u << (int)kind;
  if (!is_local_player) return;
  // Frozen target (ACHIEVEMENTS.md §5 future-proofing rule): any 7 distinct
  // weapon kinds unlock; a future kind widens the pool, never raises the bar.
  static const int WEAPONS_TARGET = 7;
  int fired = 0;
  for (int i = 0; i < 32; i++)
    if (weapons_fired_mask & (1u << i)) fired++;
  if (fired > WEAPONS_TARGET) fired = WEAPONS_TARGET;
  Achievements::progress("weapons_7", fired * 100 / WEAPONS_TARGET);
}

void Ship::add_nova_ammo(int amount) {
  for (auto it = secondary_weapons.begin(); it != secondary_weapons.end(); ++it) {
    Weapon::Nova *nw = dynamic_cast<Weapon::Nova*>(*it);
    if (nw) {
      if (nw->ammo() >= NOVA_MAX_AMMO) return;  // already at cap
      nw->add_ammo(amount);
      if (nw->ammo() > NOVA_MAX_AMMO) nw->set_ammo(NOVA_MAX_AMMO);
      if (!shield_held(secondary_weapons, secondary)) {
        (*secondary)->shoot(false);
        secondary = it;
      }
      return;
    }
  }
  Weapon::Nova *w = new Weapon::Nova(this);
  w->set_ammo(std::min(amount, NOVA_MAX_AMMO));
  secondary_weapons.push_back(w);
  if (!shield_held(secondary_weapons, secondary))
    secondary = --secondary_weapons.end();
}

int Ship::nova_ammo() const {
  for (auto it = secondary_weapons.cbegin(); it != secondary_weapons.cend(); ++it) {
    const Weapon::Nova *nw = dynamic_cast<const Weapon::Nova*>(*it);
    if (nw) return nw->ammo();
  }
  return 0;
}

void Ship::nova_detonate() {
  if (is_local_player)
    Achievements::unlock("nova_detonated");

  if (giga_mine_explode_sound != NULL)
    Mix_PlayChannel(-1, giga_mine_explode_sound, 0);

  // Single nova shockwave: expands slowly enough (speed 1.5 u/ms) to catch children
  // spawned by dying parents (min child radius 30 > 1.5*16 = 24 units/frame).
  // Radius matches one viewport: camera sits at z=1000 with 85° FOV, giving
  // a half-height of tan(42.5°)*1000 ≈ 916 world units. Fixed in world-space
  // so it is independent of screen resolution.
  const float max_r = 1500.0f;
  const float speed = 1.5f;
  shockwaves.push_back(Shockwave(position, max_r, speed, max_r / speed, true));
}

int Ship::god_mode_time_remaining() const {
  for(auto it = primary_weapons.begin(); it != primary_weapons.end(); ++it) {
    Weapon::GodMode *gm = dynamic_cast<Weapon::GodMode*>(*it);
    if(gm) return gm->time_remaining();
  }
  return 0;
}

bool Ship::shield_active() const {
  for(auto it = secondary_weapons.begin(); it != secondary_weapons.end(); ++it) {
    if(dynamic_cast<Weapon::Shield*>(*it)) {
      return invincible && god_mode_time_remaining() == 0;
    }
  }
  return false;
}

Save::Player Ship::capture_state() const {
  Save::Player p;
  p.score = score;
  p.lives      = lives;
  p.respawning = !alive;  // if mid-countdown, restore_state will consume the life via respawn(true)
  p.kills           = kills;
  p.kills_this_life = kills_this_life;
  p.pos_x           = position.x();
  p.pos_y           = position.y();
  p.facing_x        = facing.x();
  p.facing_y        = facing.y();
  p.vel_x           = velocity.x();
  p.vel_y           = velocity.y();
  p.nova_charge       = nova_charge;
  p.nova_kill_counter = nova_kill_counter;
  p.asteroid_kills       = asteroid_kills;
  p.enemy_kills          = enemy_kills;
  p.died_this_generation = died_this_generation;
  p.weapons_fired_mask   = weapons_fired_mask;

  // Primary weapons
  list<Weapon::Base*>::const_iterator cprimary = primary;
  p.selected_primary_idx = 0;
  int idx = 0;
  for (auto it = primary_weapons.cbegin(); it != primary_weapons.cend(); ++it, ++idx) {
    if (it == cprimary) p.selected_primary_idx = idx;
    Save::WeaponEntry we;
    Weapon::GodMode *gm = dynamic_cast<Weapon::GodMode*>(*it);
    if (gm) {
      we.kind         = Save::WeaponEntry::Kind::GodMode;
      we.weapon_index = -1;
      we.ammo         = gm->time_remaining();
    } else if (dynamic_cast<Weapon::Beam*>(*it)) {
      we.kind         = Save::WeaponEntry::Kind::Beam;
      we.weapon_index = -1;
      we.ammo         = (*it)->ammo();
    } else if (dynamic_cast<Weapon::Lance*>(*it)) {
      we.kind         = Save::WeaponEntry::Kind::Lance;
      we.weapon_index = -1;
      we.ammo         = (*it)->ammo();
    } else if (dynamic_cast<Weapon::Shock*>(*it)) {
      we.kind         = Save::WeaponEntry::Kind::Shock;
      we.weapon_index = -1;
      we.ammo         = (*it)->ammo();
    } else {
      Weapon::Default *dw = dynamic_cast<Weapon::Default*>(*it);
      we.kind         = Save::WeaponEntry::Kind::Default;
      we.weapon_index = dw ? dw->weapon_index() : -1;
      we.ammo         = (*it)->ammo();
    }
    p.primary_weapons.push_back(we);
  }

  // Secondary weapons
  list<Weapon::Base*>::const_iterator csecondary = secondary;
  p.selected_secondary_idx = -1;
  idx = 0;
  for (auto it = secondary_weapons.cbegin(); it != secondary_weapons.cend(); ++it, ++idx) {
    if (it == csecondary) p.selected_secondary_idx = idx;
    Save::WeaponEntry we;
    we.ammo         = (*it)->ammo();
    we.weapon_index = -1;
    if      (dynamic_cast<Weapon::Mine*>(*it))     we.kind = Save::WeaponEntry::Kind::Mine;
    else if (dynamic_cast<Weapon::GigaMine*>(*it)) we.kind = Save::WeaponEntry::Kind::GigaMine;
    else if (dynamic_cast<Weapon::Missile*>(*it))  we.kind = Save::WeaponEntry::Kind::Missile;
    else if (dynamic_cast<Weapon::Shield*>(*it))   we.kind = Save::WeaponEntry::Kind::Shield;
    else if (dynamic_cast<Weapon::Nova*>(*it))     we.kind = Save::WeaponEntry::Kind::Nova;
    else we.kind = Save::WeaponEntry::Kind::Mine; // fallback
    p.secondary_weapons.push_back(we);
  }

  return p;
}

// Classify a live secondary weapon into its Save kind (mirrors the
// capture_state dispatch), for the roster-match fast path.
static Save::WeaponEntry::Kind secondary_kind(Weapon::Base *w) {
  if      (dynamic_cast<Weapon::Mine*>(w))     return Save::WeaponEntry::Kind::Mine;
  else if (dynamic_cast<Weapon::GigaMine*>(w)) return Save::WeaponEntry::Kind::GigaMine;
  else if (dynamic_cast<Weapon::Missile*>(w))  return Save::WeaponEntry::Kind::Missile;
  else if (dynamic_cast<Weapon::Shield*>(w))   return Save::WeaponEntry::Kind::Shield;
  else if (dynamic_cast<Weapon::Nova*>(w))     return Save::WeaponEntry::Kind::Nova;
  return Save::WeaponEntry::Kind::Mine;  // capture_state's fallback
}

// True when the live weapon roster matches the snapshot's (so restore only
// needs ammo/selection, not a rebuild). Conservative: any GodMode weapon
// (its ammo is a live countdown with music/warning state) forces the slow
// path, as does any count or kind/index divergence.
bool Ship::net_weapons_roster_matches(const Save::Player &p) const {
  if (primary_weapons.size() != p.primary_weapons.size()) return false;
  if (secondary_weapons.size() != p.secondary_weapons.size()) return false;
  auto pit = primary_weapons.begin();
  for (const auto &we : p.primary_weapons) {
    if (we.kind == Save::WeaponEntry::Kind::GodMode) return false;
    Weapon::Default *dw = dynamic_cast<Weapon::Default*>(*pit);
    if (!dw) return false;  // a live GodMode (or other) where a Default is expected
    if (dw->weapon_index() != we.weapon_index) return false;
    ++pit;
  }
  auto sit = secondary_weapons.begin();
  for (const auto &we : p.secondary_weapons) {
    if (secondary_kind(*sit) != we.kind) return false;
    ++sit;
  }
  return true;
}

void Ship::restore_state(const Save::Player &p, const Grid &grid) {
  score           = p.score;
  lives           = p.lives;
  kills           = p.kills;
  kills_this_life = p.kills_this_life;
  nova_charge       = p.nova_charge;
  nova_kill_counter = p.nova_kill_counter;
  asteroid_kills       = p.asteroid_kills;
  enemy_kills          = p.enemy_kills;
  died_this_generation = p.died_this_generation;
  // Restored before the weapon loop below; add_god_mode() may re-OR its bit.
  weapons_fired_mask   = p.weapons_fired_mask;
  position        = WrappedPoint(p.pos_x, p.pos_y);
  first_life      = true;  // tells respawn() to try the saved position first

  // Fast path (netplay applies this 10x/s): when the weapon ROSTER is
  // unchanged — same counts, same primary weapon_indices, same secondary
  // kinds, and no timed GodMode weapon (its state is fiddly) — the incoming
  // snapshot differs only in ammo and selection. Update those in place
  // instead of deleting and re-newing the whole arsenal. A roster change
  // (weapon picked up, ammo hit zero and removed) falls through to the
  // rebuild below.
  if (net_weapons_roster_matches(p)) {
    auto pit = primary_weapons.begin();
    for (const auto &we : p.primary_weapons) { (*pit)->set_ammo(we.ammo); ++pit; }
    auto sit = secondary_weapons.begin();
    for (const auto &we : p.secondary_weapons) { (*sit)->set_ammo(we.ammo); ++sit; }

    primary = primary_weapons.begin();
    if (!primary_weapons.empty())
      std::advance(primary, std::min(p.selected_primary_idx,
                                     (int)primary_weapons.size() - 1));
    if (p.selected_secondary_idx >= 0 && !secondary_weapons.empty()) {
      secondary = secondary_weapons.begin();
      std::advance(secondary, std::min(p.selected_secondary_idx,
                                       (int)secondary_weapons.size() - 1));
    } else {
      secondary = secondary_weapons.end();
    }

    if (!p.respawning) alive = true;
    respawn(grid, p.respawning);
    facing   = Point(p.facing_x, p.facing_y);
    velocity = Point(p.vel_x, p.vel_y);
    return;
  }

  disable_weapons();
  primary   = primary_weapons.end();
  secondary = secondary_weapons.end();

  for (const auto &we : p.primary_weapons) {
    if (we.kind == Save::WeaponEntry::Kind::GodMode) {
      add_god_mode(we.ammo);
    } else if (we.kind == Save::WeaponEntry::Kind::Beam) {
      Weapon::Beam *w = new Weapon::Beam(this);
      w->set_ammo(we.ammo);
      primary_weapons.push_back(w);
      primary = --primary_weapons.end();
    } else if (we.kind == Save::WeaponEntry::Kind::Lance) {
      Weapon::Lance *w = new Weapon::Lance(this);
      w->set_ammo(we.ammo);
      primary_weapons.push_back(w);
      primary = --primary_weapons.end();
    } else if (we.kind == Save::WeaponEntry::Kind::Shock) {
      // Construct directly (like the Default branch) to preserve list order;
      // selected_primary_idx is re-clamped after the loop.
      Weapon::Shock *w = new Weapon::Shock(this);
      w->set_ammo(we.ammo);
      primary_weapons.push_back(w);
      primary = --primary_weapons.end();
    } else {
      // Bypass add_weapon(): it rejects weapon_index==-1 (base weapon) and
      // ignores saved ammo. Construct directly and restore ammo explicitly.
      Weapon::Default *w;
      if (we.weapon_index < 0 || we.weapon_index >= num_weapon_configs) {
        w = new Weapon::Default(this);  // base weapon, unlimited ammo
      } else {
        const WeaponConfig &cfg = weapon_configs[we.weapon_index];
        w = new Weapon::Default(this, cfg.automatic, cfg.level, cfg.accuracy,
                                cfg.time_between_shots, we.weapon_index);
        w->set_ammo(we.ammo);
      }
      primary_weapons.push_back(w);
      primary = --primary_weapons.end();
    }
  }
  if (!primary_weapons.empty()) {
    int clamp = std::min(p.selected_primary_idx, (int)primary_weapons.size() - 1);
    primary = primary_weapons.begin();
    std::advance(primary, clamp);
  }

  for (const auto &we : p.secondary_weapons) {
    switch (we.kind) {
      case Save::WeaponEntry::Kind::Mine:     add_mine_ammo(we.ammo);     break;
      case Save::WeaponEntry::Kind::GigaMine: add_giga_mine_ammo(we.ammo); break;
      case Save::WeaponEntry::Kind::Missile:  add_missile_ammo(we.ammo);  break;
      case Save::WeaponEntry::Kind::Shield:   add_shield_ammo(we.ammo);   break;
      case Save::WeaponEntry::Kind::Nova:     add_nova_ammo(we.ammo);     break;
      default: break;
    }
  }
  if (p.selected_secondary_idx >= 0 && !secondary_weapons.empty()) {
    int clamp = std::min(p.selected_secondary_idx, (int)secondary_weapons.size() - 1);
    secondary = secondary_weapons.begin();
    std::advance(secondary, clamp);
  } else {
    secondary = secondary_weapons.end();
  }

  // If the player was alive when saved, alive==false right now (Ship ctor default)
  // but respawn() needs alive||lives>0 to proceed. Set alive=true so it passes
  // even when lives==0 (e.g. last life, actively playing).
  if (!p.respawning) alive = true;
  respawn(grid, p.respawning);
  // respawn()'s reset() zeroes velocity; restore after.
  // facing is not touched by reset() but set here for clarity.
  facing   = Point(p.facing_x, p.facing_y);
  velocity = Point(p.vel_x, p.vel_y);
}

void Ship::set_missile_asteroids(std::list<Object*> *asteroids) {
  missile_asteroids = asteroids;
  for(auto it = secondary_weapons.begin(); it != secondary_weapons.end(); ++it) {
    Weapon::Missile *mw = dynamic_cast<Weapon::Missile*>(*it);
    if(mw) { mw->set_asteroids(asteroids); return; }
  }
}

void Ship::set_missile_ships(std::list<Object*> *ships) {
  missile_ships_list = ships;
  for(auto it = secondary_weapons.begin(); it != secondary_weapons.end(); ++it) {
    Weapon::Missile *mw = dynamic_cast<Weapon::Missile*>(*it);
    if(mw) { mw->set_ship_targets(ships); return; }
  }
}

void Ship::set_shock_targets(std::list<Object*> *hostiles) {
  shock_targets = hostiles;
}

// A shock bolt reached `other` (an enemy ship). Kill and credit it, mirroring
// the bullet/missile ship-kill paths. Called from GLGame, which owns the
// enemy/station lists; asteroid hits are handled in collide_grid() instead.
void Ship::shock_hit_ship(Ship *other) {
  if(!other->is_alive()) return;
  if(other->kill()) {
    other->velocity.zero();
    credit_ship_kill(other);
  }
}

void Ship::set_black_holes(const std::list<BlackHole*> *bhs) {
  black_holes = bhs;
}

void Ship::init(bool no_friction) {
  mass = 100.0;
  value = 200;
  lives = 4;
  width = height = radius = 15;
  radius_squared = radius * radius;
  respawn_time = time_until_respawn = 4000;
  max_temperature = 100.0;
  critical_temperature = max_temperature * 0.80;
  explode_temperature = max_temperature * 1.2;
  // Heat mechanic is currently DISABLED: heat_rate and boost_heat are zeroed,
  // so temperature never rises, overheating (explode_temperature) can never
  // trigger, and the HUD gauge stays hidden (GLShip::draw_temperature* early-
  // return when heat_rate <= 0). To re-enable, give heat_rate / boost_heat
  // positive values; the gauge, warnings, and overheat-detonation come back.
  heat_rate = 0.000;
  retro_heat_rate = heat_rate * -reverse_force / thrust_force;
  cool_rate = retro_heat_rate * 0.9;
  boost_heat = 0.000;
  boost_force = 4.0;
  boosting = false;
  rotation_scale = 1.0f;
  thrust_analog  = 1.0f;
  reverse_analog = 1.0f;

  if(no_friction) {
    friction = 0;
    reverse_force = -0.01;
    thrust_force = 0.03;
    rotation_force = 0.5;
  } else {
    friction = 0.003;
    reverse_force = -0.1;
    thrust_force = 0.2;
    rotation_force = 0.5;
  }

  primary_weapons.push_front(new Weapon::Default(this));
  primary = primary_weapons.begin();

  secondary = secondary_weapons.end();

  facing = Point(0, 1);
  reset();
}

float Ship::temperature_ratio() {
  return temperature/max_temperature;
}

void Ship::respawn(const Grid &grid, bool was_killed) {
  if(alive || lives > 0) {
    net_warp_count++;  // discontinuous move — netplay clients must not blend
    bool try_current_position = first_life;
    if(first_life) {
      first_life = false;
      position.wrap();
    } else {
      position = WrappedPoint();
    }
    if(was_killed) {
      lives -= 1;
    }
    alive = true;
    time_left_invincible = 1500;
    reset(was_killed);
    safe_position(grid, try_current_position);
    invincible = true;
    // Net client: snapshot restores run through here 10x/s — the hum is
    // driven exclusively by the snapshot extras there (see net_quiet_respawn).
    if(god_mode_time_remaining() <= 0 && !net_quiet_respawn) set_shield_hum(true);
    detonate();
  }
}

void Ship::safe_position(const Grid &grid, bool try_current) {
  auto in_black_hole_pull = [this]() {
    if(!black_holes) return false;
    for(const BlackHole *bh : *black_holes) {
      Point diff = bh->position.closest_to(position) - position;
      if(diff.magnitude_squared() < BlackHole::influence_radius * BlackHole::influence_radius)
        return true;
    }
    return false;
  };
  if(try_current && grid.collide(*this, 50.0f) == NULL && !in_black_hole_pull())
    return;
  do {
    position = WrappedPoint();
  } while(grid.collide(*this, 50.0f) != NULL || in_black_hole_pull());
}

void Ship::reset(bool was_killed) {
  velocity = Point(0, 0);
  thrusting = false;
  reversing = false;
  shoot(false);
  net_queued_shot_presses = 0;  // don't fire stale presses across a respawn
  // On a net client reset() runs inside EVERY 10 Hz snapshot restore
  // (restore_state → respawn) — the same story as debris/lance_pulses
  // below. Clearing the projectile lists here emptied them moments before
  // nx_read_projectiles swapped them out as the "old" lists for its
  // vanish detection, so a mine or missile the host detonated was never
  // seen disappearing — the client showed NO mine/missile explosions at
  // all (and the nova-arrival boom re-counted from zero every apply).
  // The snapshot extras own these lists on a client (wholesale-rebuilt
  // per apply); the clears are for real offline/host respawns.
  if (!net_quiet_respawn) {
    mines.clear();
    giga_mines.clear();
    bullets.clear();
    missiles.clear();
    shockwaves.clear();
    shocks.clear();
  }
  // Lance pulses are presentation like debris (below): a net client's
  // restore_state must not cut the 250 ms flash short.
  if (!net_quiet_respawn) lance_pulses.clear();
  lance_pulse_pending = false;
  // Debris is pure presentation and is never serialized. On a net client
  // reset() runs inside EVERY 10 Hz snapshot restore (restore_state →
  // respawn), so clearing it there cut every impact spray to <100 ms —
  // debris froze at the impact site and vanished. The projectile vectors
  // above are refilled by the snapshot extras right after; debris has no
  // such source, so the client keeps it for its natural lifetime.
  if (!net_quiet_respawn) debris.clear();
  rotation_direction = NONE;
  still_rotating_left = false;
  still_rotating_right = false;
  temperature = 0.0;
  if(was_killed) {
    kills_this_life = 0;
    nova_charge = 0;
    nova_kill_counter = 0;

    has_last_primary = false;  // the weapons this history names die below

    // Remove all upgraded primary weapons, keeping only the base PEW PEW at the front
    auto it = primary_weapons.begin();
    ++it;
    while(it != primary_weapons.end()) {
      delete *it;
      it = primary_weapons.erase(it);
    }
    primary = primary_weapons.begin();

    // Remove all secondary weapons (pickup-only)
    while(!secondary_weapons.empty()) {
      delete secondary_weapons.back();
      secondary_weapons.pop_back();
    }
    secondary = secondary_weapons.end();
  }
}

bool Ship::kill() {
  if(CompositeObject::kill()) {
    died_this_generation = true;  // no-damage/black-hole-survivor tracking
    thrusting = false;
    reversing = false;
    rotation_direction = NONE;
    still_rotating_left = false;
    still_rotating_right = false;
    temperature = 0.0;
    time_until_respawn = respawn_time;
    if(boost_sound != NULL) {
      Mix_VolumeChunk(boost_sound, 0);
    }
    set_shield_hum(false);
    stop_god_mode_music();
    if(explode_sound != NULL && sound_volume_scale > 0.0f) {
      // Every play site sets the chunk volume first: online, the net
      // event handler reuses this per-instance chunk at other volumes.
      Mix_VolumeChunk(explode_sound,
                      (int)(MIX_MAX_VOLUME *
                            (sound_volume_scale > 1.0f ? 1.0f : sound_volume_scale)));
      Mix_PlayChannel(-1, explode_sound, 0);
    }
    net_booms.push_back(this);  // net host relays world actors' deaths
    return true;
  }
  return false;
}

bool Ship::kill_stop() {
  if(kill()) {
    velocity.zero();
    return true;
  }
  return false;
}

bool Ship::is_removable() const {
  return CompositeObject::is_removable() && (lives == 0) && bullets.empty();
}

bool Ship::is_alive() const {
  return alive;
}

void Ship::thrust(bool on) {
  thrusting = on;
  if(boost_sound != NULL) {
    if(on && !reversing) {
      Mix_VolumeChunk(boost_sound, MIX_MAX_VOLUME/8);
    }
    if(!on && !reversing) {
      if(still_rotating_left || still_rotating_right) {
        Mix_VolumeChunk(boost_sound, MIX_MAX_VOLUME/16);
      } else {
        Mix_VolumeChunk(boost_sound, 0);
      }
    }
  }
}

void Ship::reverse(bool on) {
  reversing = on;
  if(boost_sound != NULL) {
    if(on && !thrusting) {
      Mix_VolumeChunk(boost_sound, MIX_MAX_VOLUME/8);
    }
    if(!on && !thrusting) {
      if(still_rotating_left || still_rotating_right) {
        Mix_VolumeChunk(boost_sound, MIX_MAX_VOLUME/16);
      } else {
        Mix_VolumeChunk(boost_sound, 0);
      }
    }
  }
}

void Ship::boost() {
  net_boost_count++;
  boosting = true;
}

void Ship::collide(Ship* first, Ship* second) {
  // Weapon-to-ship collisions
  //TODO: DRYness
  first->collide(second);
  second->collide(first);

  // Body-to-body collision: if the two ships physically overlap, kill the
  // non-invincible one(s).  If both are invincible nothing happens.
  if(first->is_alive() && second->is_alive() && first->Object::collide(*second)) {
    if(!first->invincible && !second->invincible) {
      first->kill_stop();
      first->detonate();
      second->kill_stop();
      second->detonate();
    } else if(!first->invincible) {
      first->kill_stop();
      first->detonate();
      if(second->is_local_player && !first->is_local_player && second->shield_active())
        Achievements::unlock("shield_ram");
      else if(net_report_bounces && second->player_ship && !second->is_local_player &&
              !first->is_local_player && second->shield_active())
        net_ach_relays.push_back({second, 1});  // Net::ACH_SHIELD_RAM
    } else if(!second->invincible) {
      second->kill_stop();
      second->detonate();
      if(first->is_local_player && !second->is_local_player && first->shield_active())
        Achievements::unlock("shield_ram");
      else if(net_report_bounces && first->player_ship && !first->is_local_player &&
              !second->is_local_player && first->shield_active())
        net_ach_relays.push_back({first, 1});  // Net::ACH_SHIELD_RAM
    }
  }
}

int Ship::multiplier() const {
  return kills_this_life / 10 + 1;
}

void Ship::collide_grid(Grid &grid, int delta) {
  Object * object;

  if(alive) {
    object = grid.collide(*this);
    if(object != NULL) {
      if(god_mode_time_remaining() > 0) {
        bool was_invincible = object->invincible;
        object->invincible = false;
        if(object->kill()) {
          object->invincible = was_invincible;
          if(was_invincible) Asteroid::num_killable++;
          score += object->get_value() * multiplier() * (was_invincible ? 5 : 1);
          credit_asteroid_kill(object);
        } else {
          object->invincible = was_invincible;
        }
      } else {
        // Check if this is an armoured asteroid hit on its armoured face.
        // Use the same arc test as the bullet handler: approach direction vs armour_angle.
        Asteroid *ast = dynamic_cast<Asteroid*>(object);
        bool armour_blocked = false;
        if(ast && ast->armoured) {
          Point approach = (object->position - position).normalized();
          float shield_dot = -(approach.x() * cosf(ast->armour_angle) +
                                approach.y() * sinf(ast->armour_angle));
          armour_blocked = (shield_dot > cosf(2.0f * (float)M_PI / 3.0f));
        }

        if(armour_blocked) {
          // Armoured face: ship is destroyed but asteroid is unharmed.
          explode(position, object->velocity);
          net_ship_impacts.push_back({this, true});
          if(Asteroid::ting_sound != NULL) {
            static Uint32 last_armour_ting = UINT32_MAX;
            Uint32 now = SDL_GetTicks();
            if(now - last_armour_ting >= 125) {
              last_armour_ting = now;
              Mix_PlayChannel(-1, Asteroid::ting_sound, 0);
            }
          }
        } else if(object->kill()) {
          detonate();
          // Shielded ram survived: the burst above went into OUR bullets
          // host-side, but a net client skips its own-ship bullet echo
          // (locally simulated, PROTO 14) — relay so the rammer sees it.
          // Fatal rams need no relay: the extras' death detonate covers
          // them client-side.
          if(net_report_bounces && player_ship && !is_local_player && invincible)
            net_ram_blasts.push_back(this);
          if(is_local_player && shield_active())
            Achievements::unlock("shield_ram_asteroid");
          else if(net_report_bounces && player_ship && shield_active())
            net_ach_relays.push_back({this, 2});  // Net::ACH_SHIELD_RAM_ASTEROID
        } else {
          explode(position, object->velocity);
          {
            // Non-fatal bounce (shielded/invincible contact): queue the
            // impact for the net client, rate-limited — contact persists
            // across many 8 ms steps and would flood the channel.
            static Uint32 last_net_ship_impact = UINT32_MAX;
            Uint32 now_imp = SDL_GetTicks();
            if(now_imp - last_net_ship_impact >= 125) {
              last_net_ship_impact = now_imp;
              net_ship_impacts.push_back({this, false});
            }
          }
          // The hit object was invincible. If the ship is also invincible
          // (shielded), neither side died — but we still need to check whether
          // the ship is simultaneously touching a killable asteroid, because
          // grid.collide() stops at the first hit and would have skipped it.
          if(invincible) {
            Object *killable = grid.collide(*this, 0.0f, true);
            if(killable != NULL && killable->kill()) {
              detonate();
              // Same own-ship-echo gap as the primary ram-kill above.
              if(net_report_bounces && player_ship && !is_local_player)
                net_ram_blasts.push_back(this);
              if(is_local_player && shield_active())
                Achievements::unlock("shield_ram_asteroid");
              else if(net_report_bounces && player_ship && shield_active())
                net_ach_relays.push_back({this, 2});  // Net::ACH_SHIELD_RAM_ASTEROID
            }
          }
        }
      }
      kill_stop();
    }
  }

  for(size_t i = 0; i < mines.size(); ) {
    object = grid.collide(mines[i], 50.0f);
    if(object != NULL && object->alive) {
      NET_LOG("net: mine detonated at (%.0f, %.0f)\n",
              mines[i].position.x(), mines[i].position.y());
      // MINE_SHRAPNEL (20) -> 10-29 shrapnel bullets. Was 50 (25-74), which
      // flooded the world with bullets when several mines detonated together
      // and tanked the frame rate.
      detonate(mines[i].position, mines[i].velocity, MINE_SHRAPNEL);
      if(mine_explode_sound != NULL) Mix_PlayChannel(-1, mine_explode_sound, 0);
      mines[i] = std::move(mines.back());
      mines.pop_back();
    } else {
      ++i;
    }
  }

  for(size_t i = 0; i < giga_mines.size(); ) {
    object = grid.collide(giga_mines[i], 50.0f);
    if(object != NULL && object->alive) {
      giga_detonate(giga_mines[i].position);
      giga_mines[i] = std::move(giga_mines.back());
      giga_mines.pop_back();
    } else {
      ++i;
    }
  }

  // Process active shockwaves: kill everything in the expanding ring
  for(auto &sw : shockwaves) {
    if(!missile_asteroids) continue;
    for(auto it = missile_asteroids->begin(); it != missile_asteroids->end(); ++it) {
      Object *obj = *it;
      if(!obj->alive || obj->invincible) continue;
      // Nova shockwaves use wrapped distance so the ring reaches across world edges.
      // Regular (giga-mine) shockwaves use raw distance as before.
      float dist = sw.is_nova
        ? obj->position.distance_to(WrappedPoint(sw.position.x(), sw.position.y()))
        : (obj->position - sw.position).magnitude();
      // Nova: kill everything inside the expanding circle so children spawned
      // behind the ring front are caught on the very next frame.
      // Giga-mine: ring-sweep only (between prev_radius and current_radius).
      bool in_kill_zone = sw.is_nova
        ? dist <= sw.radius
        : (dist <= sw.radius && dist > sw.prev_radius - obj->radius);
      if(in_kill_zone) {
        if(obj->kill()) {
          score += obj->get_value() * multiplier();
          credit_asteroid_kill(obj, !sw.is_nova);  // no nova feedback from nova's own kills
        }
      }
    }
  }

  // Shock bolts: damage the asteroids their arcs reached this frame. Non-asteroid
  // targets (enemies, stations, hazards) are left in `struck` for GLGame to
  // handle, which owns those lists and their damage APIs. This runs on the host /
  // single player only — the net client never calls collide_grid, so it drains
  // its shock struck lists (asteroid claims included) in GLGame::tick_net_client.
  //
  // The arc only chains onward from a *killing* hit: if the asteroid survives
  // the hit (tough with health left, phasing while a ghost, teleporting
  // mid-evade), the bolt stops where it is instead of arcing past to the next
  // target. Invincible asteroids never appear here: they are not sought and
  // grow_segment stops the bolt at their surface without a struck entry.
  for(auto &bolt : shocks) {
    for(size_t i = 0; i < bolt.struck.size(); ) {
      Object *obj = bolt.struck[i];
      if(dynamic_cast<Asteroid*>(obj)) {
        if(obj->alive) {
          if(obj->kill()) {
            score += obj->get_value() * multiplier();
            credit_asteroid_kill(obj);  // destroyed — arc chains onward
          } else {
            bolt.stop();  // survived the hit — arc ends here
          }
        }
        bolt.struck[i] = bolt.struck.back();
        bolt.struck.pop_back();
      } else {
        ++i;
      }
    }
  }

  collide_bullets_with_asteroids(grid, delta);

  // World bullets (ricocheted off reflective asteroids) can kill their owner.
  for(size_t i = 0; i < bullets.size(); ) {
    if(bullets[i].world_bullet && is_alive() && bullets[i].collide(*this)) {
      kill_stop();
      explode(bullets[i].position, bullets[i].velocity);
      bullets[i] = std::move(bullets.back());
      bullets.pop_back();
    } else {
      ++i;
    }
  }

  for(size_t i = 0; i < missiles.size(); ) {
    object = grid.collide(missiles[i], 5.0f);
    if(object != NULL && object->alive) {
      if(object->kill()) {
        score += object->get_value() * multiplier();
        credit_asteroid_kill(object);
      }
      detonate(missiles[i].position, missiles[i].velocity, MISSILE_SHRAPNEL);
      if(missile_explode_sound != NULL) {
        Mix_PlayChannel(-1, missile_explode_sound, 0);
      }
      missiles[i] = std::move(missiles.back());
      missiles.pop_back();
    } else {
      ++i;
    }
  }
}

// Bullet-vs-asteroid collision, factored out so non-player Ships (e.g. the
// roaming mini-station) can reuse the exact same bullet behaviour against
// asteroids — reflective ricochets, armoured deflection, swept hit tests and
// all. Score is added to this ship's own counter; for non-player ships that
// counter is simply never displayed, so no points reach the players.
void Ship::collide_bullets_with_asteroids(const Grid &grid, int delta) {
  Object *object;
  vector<Object *> sweep_candidates;
  for(size_t i = 0; i < bullets.size(); ) {
    // Swept collision via segment-polygon intersection: test the bullet's
    // full path this frame against asteroid polygon edges so fast bullets
    // cannot tunnel through.
    Point seg_a(bullets[i].position.x() - bullets[i].velocity.x() * delta,
                bullets[i].position.y() - bullets[i].velocity.y() * delta);
    Point seg_b(bullets[i].position.x(), bullets[i].position.y());
    sweep_candidates.clear();
    grid.query_segment(seg_a, seg_b, sweep_candidates);
    object = NULL;
    float best_t = 2.0f;
    for (Object *cand : sweep_candidates) {
      Asteroid *ast = dynamic_cast<Asteroid*>(cand);
      if (!ast) continue;
      // When the bullet's path crosses a world edge, seg_a can lie outside
      // [x_min, x_max] while the asteroid sits near the opposite edge.
      // Translate both endpoints into the same world-copy as the asteroid so
      // the segment/polygon tests produce correct results.
      Point ast_near = ast->position.closest_to(seg_a);
      float ox = ast_near.x() - ast->position.x();
      float oy = ast_near.y() - ast->position.y();
      Point local_a(seg_a.x() - ox, seg_a.y() - oy);
      Point local_b(seg_b.x() - ox, seg_b.y() - oy);
      float t;
      bool edge_hit = ast->segment_hit(local_a, local_b, t);
      if (edge_hit && t < best_t) {
        best_t = t;
        object = cand;
      } else if (!edge_hit && ast->contains(local_b) && 1.0f < best_t) {
        // Bullet is inside the asteroid without crossing its edge this frame
        // (e.g. fired point-blank into a large asteroid, or asteroid moved
        // over the bullet). Treat as a hit at the bullet's current position.
        best_t = 1.0f;
        object = cand;
      }
    }
    if (object != NULL) {
      bullets[i].position = WrappedPoint(seg_a.x() + (seg_b.x() - seg_a.x()) * best_t,
                                         seg_a.y() + (seg_b.y() - seg_a.y()) * best_t);
    }
    if(object != NULL) {
      Asteroid *ast = dynamic_cast<Asteroid*>(object);
      if(ast && (ast->reflective || (ast->phasing && ast->phased)) && !bullets[i].kills_invincible) {
        // Back-trace along the bullet's velocity to find where it crossed the surface.
        // The bullet is guaranteed to be inside the polygon here; stepping backward
        // by 1px increments finds the entry point in ~10 steps for typical bullet speeds.
        Point vel_norm = bullets[i].velocity.normalized();
        float max_trace = ast->effective_radius() * 2.0f + 4.0f;
        WrappedPoint entry = bullets[i].position;
        for (float d = 1.0f; d <= max_trace; d += 1.0f) {
          WrappedPoint test(bullets[i].position.x() - vel_norm.x() * d,
                            bullets[i].position.y() - vel_norm.y() * d);
          if (!ast->contains(test)) {
            entry = test;
            break;
          }
        }
        // Use the edge normal most facing the bullet so corner hits reflect
        // away from the struck face, not along its bisector.
        Point rel_vel = bullets[i].velocity - object->velocity;
        Point normal = ast->surface_normal(entry, rel_vel);
        // Reflect in the asteroid's reference frame so a chasing asteroid
        // doesn't immediately catch the bullet again.
        float dot = normal.x() * rel_vel.x() + normal.y() * rel_vel.y();
        bullets[i].velocity = object->velocity + (rel_vel - normal * (2.0f * dot));
        // Push out by a full step's worth of relative travel so glancing hits
        // (tiny outward component) still clear the surface next frame.
        float push = rel_vel.magnitude() * 16.0f + 2.0f;
        bullets[i].position = WrappedPoint(entry.x() + normal.x() * push,
                                           entry.y() + normal.y() * push);
        object->kill(); // plays thud sound
        bullets[i].world_bullet = true;
        if (net_report_bounces && bullets[i].net_id != 0)
          net_bounce_reports.push_back({bullets[i].net_id,
              bullets[i].position.x(), bullets[i].position.y(),
              bullets[i].velocity.x(), bullets[i].velocity.y(),
              bullets[i].net_flags()});
        ++i;
      } else if (ast && ast->armoured && !bullets[i].kills_invincible) {
        // Armoured asteroid: back-trace to the actual surface entry point, then
        // test whether that point falls within the shielded arc (±120° from
        // armour_angle).  This matches the renderer exactly — it also tests
        // surface-point direction from center, not bullet approach direction.
        // Using approach direction instead caused reflections on unshielded faces
        // when the irregular polygon made the impact point and approach direction
        // disagree about which arc they belonged to.
        Point vel_norm = bullets[i].velocity.normalized();
        float max_trace = ast->effective_radius() * 2.0f + 4.0f;
        WrappedPoint entry = bullets[i].position;
        for (float d = 1.0f; d <= max_trace; d += 1.0f) {
          WrappedPoint test(bullets[i].position.x() - vel_norm.x() * d,
                            bullets[i].position.y() - vel_norm.y() * d);
          if (!ast->contains(test)) { entry = test; break; }
        }
        float ix = entry.x() - ast->position.x();
        float iy = entry.y() - ast->position.y();
        float im = sqrtf(ix * ix + iy * iy);
        float shield_dot = (im > 1e-6f)
          ? (ix * cosf(ast->armour_angle) + iy * sinf(ast->armour_angle)) / im
          : -1.0f;
        Point rel_vel = bullets[i].velocity - object->velocity;
        if (shield_dot > -0.5f) {
          // Hit the armoured face — reflect bullet, do NOT damage the asteroid
          Point normal = ast->surface_normal(entry, rel_vel);
          float dot = normal.x() * rel_vel.x() + normal.y() * rel_vel.y();
          bullets[i].velocity = object->velocity + (rel_vel - normal * (2.0f * dot));
          float push = rel_vel.magnitude() * 16.0f + 2.0f;
          bullets[i].position = WrappedPoint(entry.x() + normal.x() * push,
                                             entry.y() + normal.y() * push);
          if (Asteroid::ting_sound != NULL) {
            static Uint32 last_armour_ting = UINT32_MAX;
            Uint32 now = SDL_GetTicks();
            if (now - last_armour_ting >= 125) {
              last_armour_ting = now;
              Mix_PlayChannel(-1, Asteroid::ting_sound, 0);
            }
          }
          bullets[i].world_bullet = true;
          if (net_report_bounces && bullets[i].net_id != 0)
            net_bounce_reports.push_back({bullets[i].net_id,
                bullets[i].position.x(), bullets[i].position.y(),
                bullets[i].velocity.x(), bullets[i].velocity.y(),
                bullets[i].net_flags()});
          ++i;
        } else {
          // Hit the unarmoured face — kill normally
          if(object->kill()) {
            score += object->get_value() * multiplier();
            credit_asteroid_kill(object);
          }
          explode(bullets[i].position, object->velocity);
          bullets[i] = std::move(bullets.back());
          bullets.pop_back();
        }
      } else {
        bool was_invincible = object->invincible;
        if(bullets[i].kills_invincible) {
          object->invincible = false;
          if(ast && ast->teleporting) ast->teleport_vulnerable = true;
          if(ast && ast->tough) ast->health = 1;
        }
        bool destroyed = object->kill();
        if(destroyed) {
          object->invincible = was_invincible;
          if(was_invincible) Asteroid::num_killable++;
          score += object->get_value() * multiplier() * (was_invincible ? 5 : 1);
          credit_asteroid_kill(object);
        }
        explode(bullets[i].position, object->velocity);
        if(bullets[i].piercing && destroyed) {
          // Beam bolt ploughs on only through asteroids it actually destroys:
          // the swept collision next frame resumes from this hit point and
          // catches the next one without tunnelling. If the asteroid survived
          // the hit (invincible, or tough not yet broken) the bolt stops here.
          ++i;
        } else {
          bullets[i] = std::move(bullets.back());
          bullets.pop_back();
        }
      }
    } else {
      ++i;
    }
  }
}

void Ship::fire_lance_pulse(const Grid &grid) {
  // Instantaneous full-length pulse: ray-march up to Weapon::Lance::RANGE of
  // total path. Killable asteroids along the line die and the pulse continues
  // through them — including TOUGH asteroids, which the lance kills outright;
  // surfaces that reflect bullets (reflective asteroids, armoured faces,
  // phased ghosts) mirror-reflect it with its remaining distance; anything it
  // cannot destroy (plain invincible, a teleport evade) blocks it.
  float fm = facing.magnitude();
  if(fm <= 0.0f) return;
  Point dir = facing * (1.0f / fm);
  WrappedPoint pos = gun();
  float remaining = Weapon::Lance::RANGE;

  LancePulse pulse;
  pulse.ttl = pulse.time_left = 250.0f;
  pulse.points.push_back(Point(pos.x(), pos.y()));

  // PROTO 18 (net client): asteroids this pulse already claimed — they stay
  // alive until the claim drain kills them later this tick, so the march
  // must not re-strike them (offline/host kills leave them !is_alive()).
  vector<Asteroid *> claimed;

  vector<Object *> candidates;
  const int max_hits = 16;  // safety cap on kills+reflections per pulse
  for(int hit_count = 0; hit_count < max_hits && remaining > 1.0f; hit_count++) {
    Point seg_a(pos.x(), pos.y());
    Point seg_b(seg_a.x() + dir.x() * remaining, seg_a.y() + dir.y() * remaining);
    candidates.clear();
    grid.query_segment(seg_a, seg_b, candidates);

    // Nearest asteroid the segment crosses (same wrapped-world translation as
    // the swept bullet collision above).
    Asteroid *ast_hit = NULL;
    float best_t = 2.0f;
    Point best_entry;  // hit point in the asteroid's own world-copy
    for(Object *cand : candidates) {
      Asteroid *ast = dynamic_cast<Asteroid *>(cand);
      if(!ast || !ast->is_alive()) continue;
      if(std::find(claimed.begin(), claimed.end(), ast) != claimed.end()) continue;
      Point ast_near = ast->position.closest_to(seg_a);
      float ox = ast_near.x() - ast->position.x();
      float oy = ast_near.y() - ast->position.y();
      Point local_a(seg_a.x() - ox, seg_a.y() - oy);
      Point local_b(seg_b.x() - ox, seg_b.y() - oy);
      float t;
      // Ignore sub-unit hits so a fresh reflection can't re-strike the edge
      // it just left.
      if(ast->segment_hit(local_a, local_b, t) && t < best_t && t * remaining > 1.0f) {
        best_t = t;
        ast_hit = ast;
        best_entry = Point(local_a.x() + (local_b.x() - local_a.x()) * t,
                           local_a.y() + (local_b.y() - local_a.y()) * t);
      }
    }

    if(ast_hit == NULL) {
      // Clear line: the pulse runs out at full remaining length.
      pulse.points.push_back(seg_b);
      remaining = 0.0f;
      break;
    }

    float travelled = best_t * remaining;
    Point hit_world(seg_a.x() + dir.x() * travelled, seg_a.y() + dir.y() * travelled);
    pulse.points.push_back(hit_world);
    remaining -= travelled;

    bool reflect = false;
    if(ast_hit->reflective || (ast_hit->phasing && ast_hit->phased)) {
      reflect = true;
      ast_hit->kill();  // never destroys these; plays the ting/thud feedback
    } else if(ast_hit->armoured) {
      // Same shielded-arc test as the bullet handler: surface-point direction
      // from centre vs armour_angle (±120°).
      float ix = best_entry.x() - ast_hit->position.x();
      float iy = best_entry.y() - ast_hit->position.y();
      float im = sqrtf(ix * ix + iy * iy);
      float shield_dot = (im > 1e-6f)
        ? (ix * cosf(ast_hit->armour_angle) + iy * sinf(ast_hit->armour_angle)) / im
        : -1.0f;
      if(shield_dot > -0.5f) {
        reflect = true;
        if(Asteroid::ting_sound != NULL && sound_volume_scale > 0.0f) {
          Mix_PlayChannel(-1, Asteroid::ting_sound, 0);
        }
      }
    }

    if(reflect) {
      Point normal = ast_hit->surface_normal(best_entry, dir);
      float dot = normal.x() * dir.x() + normal.y() * dir.y();
      dir = dir - normal * (2.0f * dot);
      float dm = dir.magnitude();
      if(dm <= 0.0f) break;
      dir = dir * (1.0f / dm);
      // Step off the surface so the next segment starts clear of this edge.
      pos = WrappedPoint(hit_world.x() + normal.x() * 2.0f,
                         hit_world.y() + normal.y() * 2.0f);
      continue;
    }

    explode(Point(hit_world.x(), hit_world.y()), ast_hit->velocity);

    // PROTO 18 (net client, net_claim_kills): predict the outcome without
    // killing locally and queue an MSG_HIT claim (bullet_id 0 — no clone
    // to consume). The claim drain later this tick does the local kill
    // with proper removal bookkeeping, exactly like bullet claims; the
    // host honors the claim with the same tough/teleport forcing.
    if(net_claim_kills) {
      if(ast_hit->invincible ||
         (ast_hit->teleporting && !ast_hit->teleport_vulnerable)) {
        // Blocked (a teleport evade is the host's call — don't claim it).
        remaining = 0.0f;
        break;
      }
      NetKillClaim c;
      c.ast_id = ast_hit->net_id;
      c.bullet_id = 0;  // lance sentinel: honor without a clone consume
      net_kill_claims.push_back(c);
      claimed.push_back(ast_hit);
      // Claimed-dead: carry straight on through it (score/kills are
      // host-owned and arrive via the snapshot HUD scalars).
      pos = WrappedPoint(hit_world.x(), hit_world.y());
      continue;
    }

    // The lance kills tough asteroids outright (Glenn's ruling) — force the
    // health through like the host's claim handler does, then kill.
    if(ast_hit->tough) ast_hit->health = 1;
    if(ast_hit->kill()) {
      score += ast_hit->get_value() * multiplier();
      kills_this_life += 1;
      kills += 1;
      tally_nova_kill(ast_hit->position);
      // Destroyed: carry straight on through it.
      pos = WrappedPoint(hit_world.x(), hit_world.y());
      continue;
    }

    // Survived the hit (invincible or a teleport evade): the pulse is
    // blocked here.
    remaining = 0.0f;
    break;
  }

  // Report the traced polyline to the peer for its flash + sound
  // (net_report_shots covers exactly the two reporters: the client's own
  // ship and, since PROTO 17, the host's player ship).
  if(net_report_shots) net_lance_reports.push_back(pulse.points);

  // Park the polyline for GLGame's ship/station hit resolution: the
  // authoritative pass offline/host-side, or the client's enemy-replica
  // pass (instant kill + bullet_id-0 claim, PROTO 20 — station/mini hull
  // hits stay the host's, resolved from the MSG_LANCE polyline).
  lance_hit_pending = pulse.points;

  lance_pulses.push_back(std::move(pulse));
}

void Ship::collide(Ship *other) {
  for(size_t i = 0; i < bullets.size(); ) {
    if(other->is_alive() && bullets[i].collide(*other)) {
      // No kill, no credit: shots absorbed by a shield/invincibility (e.g.
      // a disconnected player's parked ship) must not award anything.
      if(other->kill_stop()) credit_ship_kill(other);
      bullets[i] = std::move(bullets.back());
      bullets.pop_back();
    } else {
      ++i;
    }
  }

  for(size_t i = 0; i < mines.size(); ) {
    if(is_alive() && other->is_alive() && mines[i].collide(*other, 50.0)) {
      // Same blast as the grid (asteroid) path above — this ship-contact
      // path was silently using detonate()'s default 10.
      detonate(mines[i].position, mines[i].velocity, MINE_SHRAPNEL);
      if(mine_explode_sound != NULL) Mix_PlayChannel(-1, mine_explode_sound, 0);
      mines[i] = std::move(mines.back());
      mines.pop_back();
    } else {
      ++i;
    }
  }

  for(size_t i = 0; i < giga_mines.size(); ) {
    if(is_alive() && other->is_alive() && giga_mines[i].collide(*other, 50.0)) {
      giga_detonate(giga_mines[i].position);
      giga_mines[i] = std::move(giga_mines.back());
      giga_mines.pop_back();
    } else {
      ++i;
    }
  }

  // Shockwave kills other ships
  for(auto &sw : shockwaves) {
    if(!other->is_alive()) break;
    float dist = (other->position - sw.position).magnitude();
    if(dist <= sw.radius && dist + other->radius > sw.prev_radius) {
      other->kill_stop();
    }
  }

  for(size_t i = 0; i < missiles.size(); ) {
    if(is_alive() && other->is_alive() && missiles[i].collide(*other, 5.0)) {
      // No credit for a shielded/invincible target.
      if(other->kill_stop()) credit_ship_kill(other);
      detonate(missiles[i].position, missiles[i].velocity, MISSILE_SHRAPNEL);
      if(missile_explode_sound != NULL) {
        Mix_PlayChannel(-1, missile_explode_sound, 0);
      }
      missiles[i] = std::move(missiles.back());
      missiles.pop_back();
    } else {
      ++i;
    }
  }
}

void Ship::detonate() {
  detonate(position, velocity);
}

void Ship::detonate(Point const position, Point const velocity, int particle_count) {
  Point dir = (facing * radius * 1.2);
  for(int i = rand()%particle_count+particle_count/2; i > 0; i--) {
    dir.rotate(rand()%360*M_PI/180);
    bullets.push_back(Particle(position + dir, velocity + dir*0.0001*(rand()%150), rand()%1500));
  }
}

void Ship::giga_detonate(Point const position) {
  if(giga_mine_explode_sound != NULL) {
    Mix_PlayChannel(-1, giga_mine_explode_sound, 0);
  }
  // Launch expanding shockwave ring: radius grows to max_radius over ~700ms
  float max_r = (float)Asteroid::max_radius;
  float duration = 700.0f;
  float speed = max_r / duration;
  shockwaves.push_back(Shockwave(position, max_r, speed, duration));
}

void Ship::shoot(bool on) {
  if(primary_weapons.empty()) return;
  if((*primary)->empty() && on) {
    // Firing an empty limited primary (beam/lance/shock) drops it from the
    // list, exactly like fire_secondary() below — it used to just switch
    // away and leave the spent weapon cluttering the cycle. The default
    // gun is unlimited, so the list never empties for real.
    auto to_remove = primary;
    auto next = fallback_primary(to_remove);
    delete *to_remove;
    primary_weapons.erase(to_remove);
    primary = primary_weapons.empty() ? primary_weapons.end() : next;
  } else {
    if(on) {
      // weapons_7 tracking: name the branch's extra primaries too, not
      // just master's GodMode/Default pair.
      Save::WeaponEntry::Kind kind = Save::WeaponEntry::Kind::Default;
      if(dynamic_cast<Weapon::GodMode*>(*primary))
        kind = Save::WeaponEntry::Kind::GodMode;
      else if(dynamic_cast<Weapon::Beam*>(*primary))
        kind = Save::WeaponEntry::Kind::Beam;
      else if(dynamic_cast<Weapon::Lance*>(*primary))
        kind = Save::WeaponEntry::Kind::Lance;
      else if(dynamic_cast<Weapon::Shock*>(*primary))
        kind = Save::WeaponEntry::Kind::Shock;
      record_weapon_fired(kind);
    }
    (*primary)->shoot(on);
  }
}

void Ship::fire_secondary(bool on) {
  if(secondary_weapons.empty()) return;
  if((*secondary)->empty() && on) {
    auto to_remove = secondary;
    auto next = to_remove;
    ++next;
    if(next == secondary_weapons.end())
      next = secondary_weapons.begin();
    delete *to_remove;
    secondary_weapons.erase(to_remove);
    secondary = secondary_weapons.empty() ? secondary_weapons.end() : next;
  } else {
    if(on) {
      Save::WeaponEntry::Kind kind = Save::WeaponEntry::Kind::Mine;
      if      (dynamic_cast<Weapon::GigaMine*>(*secondary)) kind = Save::WeaponEntry::Kind::GigaMine;
      else if (dynamic_cast<Weapon::Missile*>(*secondary))  kind = Save::WeaponEntry::Kind::Missile;
      else if (dynamic_cast<Weapon::Shield*>(*secondary))   kind = Save::WeaponEntry::Kind::Shield;
      else if (dynamic_cast<Weapon::Nova*>(*secondary))     kind = Save::WeaponEntry::Kind::Nova;
      record_weapon_fired(kind);
    }
    (*secondary)->shoot(on);
  }
}

// Net client: cosmetic bullet-vs-asteroid impacts, detected locally
// against the replicated state. The client never runs
// collide_bullets_with_asteroids (the host owns the world), so bullets
// flying into asteroids that survive a plain bullet showed neither
// debris nor a thud/ting — and shipping every impact as a host event
// costs bandwidth for a purely visual cue. Only un-killable cases spark
// here: a killable asteroid's death explosion arrives via the snapshot,
// and doubling it would flash twice. The host's copy of this bullet is
// reflected or consumed authoritatively; this copy just gets one spray
// (net_sparked) until the next snapshot correction replaces it.
void Ship::net_cosmetic_impacts(const Grid &grid, bool claim_kills) {
  for (auto &b : bullets) {
    if (b.net_sparked || b.kills_invincible) continue;
    Object *o = grid.collide(b);
    if (o == NULL) continue;
    Asteroid *ast = dynamic_cast<Asteroid *>(o);
    if (ast == NULL) continue;
    bool ting;
    bool consume = false;  // absorbed/killing hits stop the bullet HERE
    bool deflect = false;  // mirror/armour bounce, approximated locally
    if (ast->reflective || (ast->phasing && ast->phased)) {
      ting = true;  // bullet bounces off / passes through
      deflect = ast->reflective;  // phased rocks genuinely pass bullets
    } else if (ast->armoured) {
      // Shielded arc only (same ±120° test as the host's collide code,
      // sans the entry back-trace — cosmetic precision is enough): an
      // unshielded hit kills, and that explosion is replicated.
      float ix = b.position.x() - ast->position.x();
      float iy = b.position.y() - ast->position.y();
      float im = sqrtf(ix * ix + iy * iy);
      float shield_dot =
          (im > 1e-6f)
              ? (ix * cosf(ast->armour_angle) + iy * sinf(ast->armour_angle)) / im
              : -1.0f;
      if (shield_dot <= -0.5f) continue;
      ting = true;
      deflect = true;
    } else if (ast->invincible || (ast->tough && ast->health > 1) ||
               (ast->teleporting && !ast->teleport_vulnerable)) {
      ting = false;   // thud: absorbed (or the asteroid teleports away)
      consume = true; // the host consumes its copy — ours must stop too
    } else {
      // Plain killable asteroid: the KILL is the host's call (it lands
      // via the removal record ~RTT later), but the BULLET must not fly
      // on through the rock while we wait — on a relay that read as
      // shots phasing through asteroids that died a beat later. Consume
      // it at the contact point with a hit spray now; the death
      // explosion and sound still replicate. If the host's copy of this
      // shot somehow misses, we spent a spark on a miss — cosmetic.
      // PROTO 18: a piercing beam bolt claims the kill and PLOUGHS ON
      // (mirroring the sim's plough-through-kills rule) — the claim
      // drain kills the asteroid this same tick, so the next step's
      // grid no longer holds it; duplicate claims before then dedupe
      // against the prediction map at the drain.
      explode(b.position, o->velocity);
      if (claim_kills) {
        NetKillClaim c;
        c.ast_id = ast->net_id;
        c.bullet_id = b.piercing ? 0 : b.net_id;  // 0: don't consume the clone
        net_kill_claims.push_back(c);
      }
      if (!b.piercing) {
        b.net_sparked = true;
        b.time_left = 0.0f;
      }
      NET_LOG("net: cosmetic impact %s id=%u\n",
              b.piercing ? "pierce" : "consume", ast->net_id);
      continue;
    }
    b.net_sparked = true;
    if (consume) b.time_left = 0.0f;
    if (deflect) {
      // Local bullets are client-owned (no host echo replaces them any
      // more), so approximate the host's mirror/armour bounce here or
      // the shot sails straight through the rock. The host's copy does
      // the REAL bounce (which can hit things); this one is presentation
      // — net_sparked above keeps it claim-inert from here on.
      float nx = b.position.x() - ast->position.x();
      float ny = b.position.y() - ast->position.y();
      float nm = sqrtf(nx * nx + ny * ny);
      if (nm > 1e-6f) {
        nx /= nm; ny /= nm;
        float d = b.velocity.x() * nx + b.velocity.y() * ny;
        b.velocity = Point(b.velocity.x() - 2.0f * d * nx,
                           b.velocity.y() - 2.0f * d * ny);
      }
      // Match the host's recolour: a ricocheted shot turns white
      // (world_bullet) so both screens read the bounce the same way.
      b.world_bullet = true;
    }
    // Deflections spray no debris — the host's sim bounce doesn't either
    // (the bullet survives; debris reads as it breaking up).
    if (!deflect) explode(b.position, o->velocity);
    Mix_Chunk *snd = ting ? Asteroid::ting_sound : Asteroid::thud_sound;
    if (snd != NULL) {
      // Same 125 ms cue rate limit the host-side impact sites use.
      static Uint32 last_cosmetic_cue = UINT32_MAX;
      Uint32 now = SDL_GetTicks();
      if (now - last_cosmetic_cue >= 125) {
        last_cosmetic_cue = now;
        Mix_PlayChannel(-1, snd, 0);
      }
    }
    NET_LOG("net: cosmetic impact %s\n", ting ? "ting" : "thud");
  }
}

void Ship::net_cosmetic_ship_impacts(
    const std::vector<NetShipTarget> &targets) {
  if (targets.empty()) return;
  for (auto &b : bullets) {
    if (b.net_sparked || b.kills_invincible) continue;
    for (size_t t = 0; t < targets.size(); t++) {
      Object *o = targets[t].obj;
      if (!b.collide(*o)) continue;
      // Visible contact: stop the bullet here with the impact effects.
      b.net_sparked = true;
      b.time_left = 0.0f;
      explode(b.position, o->velocity);
      if (targets[t].kind == 0) {
        // PROTO 16: enemies die HERE, instantly — the claim is exact
        // (per-enemy id) and always honored, the suppression map keeps
        // restores from resurrecting the replica, and the host skips
        // the EV_WORLD_BOOM relay for claimed kills so this local boom
        // is the only cue we hear.
        Ship *e = static_cast<Ship *>(o);
        if (e->kill_stop()) {
          e->detonate();
          // Our kill: enemies_10/score progress on THIS machine (the host
          // credits its replica's counters; snapshots reconcile ours).
          credit_ship_kill(e);
        }
        // kill() stays silent (replicas keep sound_volume_scale 0 on the
        // client) — play the dying ship's per-instance chunk ourselves,
        // full volume: the kill is at our own crosshair.
        if (e->explode_sound != NULL) {
          Mix_VolumeChunk(e->explode_sound, MIX_MAX_VOLUME);
          Mix_PlayChannel(-1, e->explode_sound, 0);
        }
      } else if (Asteroid::thud_sound != NULL) {
        // Station hulls absorb the shot with a thud.
        static Uint32 last_ship_thud = UINT32_MAX;
        Uint32 now = SDL_GetTicks();
        if (now - last_ship_thud >= 125) {
          last_ship_thud = now;
          Mix_PlayChannel(-1, Asteroid::thud_sound, 0);
        }
      }
      NetShipHit c;
      c.kind = targets[t].kind;
      c.bullet_id = b.net_id;
      c.target_id = targets[t].id;
      c.x = b.position.x();
      c.y = b.position.y();
      net_ship_hit_claims.push_back(c);
      NET_LOG("net: ship impact consume kind=%u bullet=%u target=%u\n",
              (unsigned)c.kind, c.bullet_id, c.target_id);
      break;
    }
  }
}

void Ship::net_blast(const Point &pos, const Point &vel, int count) {
  NET_LOG("net: blast at (%.0f, %.0f) count=%d %s\n", pos.x(), pos.y(), count,
          net_claim_kills ? "own->bullets" : "peer->debris");
  if(net_claim_kills) {
    // Our OWN deployable (the client's local ship): blast into bullets
    // exactly like the real detonate() — the particles kill asteroids
    // locally and claim them (bullet_id 0, the no-clone sentinel), so the
    // kills feel instant. Own bullets are client-owned and survive the
    // applies.
    detonate(pos, vel, count);
    return;
  }
  // The peer's: cosmetic only — the host's authoritative blast does the
  // killing, and a remote ship's bullets are wholesale-rebuilt from the
  // record on every 10 Hz apply, which wiped a bullets-list blast within
  // ~100 ms of it appearing ("mine explosions aren't displayed on net
  // clients"). Same particle recipe as detonate(count), but into debris,
  // which the applies leave alone; the streak flag makes them draw
  // exactly like the real blast's bullets.
  Point dir = (facing * radius * 1.2);
  for(int i = rand() % count + count / 2; i > 0; i--) {
    dir.rotate(rand() % 360 * M_PI / 180);
    debris.push_back(Particle(pos + dir, vel + dir * 0.0001 * (rand() % 150),
                              rand() % 1500));
    debris.back().streak = true;
  }
}

void Ship::net_missile_exploded(const Point &pos, const Point &vel) {
  net_blast(pos, vel, MISSILE_SHRAPNEL);
  if(missile_explode_sound != NULL)
    Mix_PlayChannel(-1, missile_explode_sound, 0);
}

void Ship::net_mine_exploded(const Point &pos, const Point &vel) {
  net_blast(pos, vel, MINE_SHRAPNEL);
  if(mine_explode_sound != NULL)
    Mix_PlayChannel(-1, mine_explode_sound, 0);
}

void Ship::net_giga_mine_exploded(const Point &pos) {
  // Mirrors giga_detonate's presentation (sound + shockwave ring); the
  // host's real shockwave also arrives via the snapshot a beat later and
  // replaces this one harmlessly.
  giga_detonate(pos);
}

void Ship::net_nova_arrived() {
  if(giga_mine_explode_sound != NULL)
    Mix_PlayChannel(-1, giga_mine_explode_sound, 0);
}

// PROTO 22: a shock bolt the peer fired arrived as a polyline. Show it on
// this (the firer's replica) ship as a display-only bolt: fully grown, just
// fades, and — crucially — its exact segments are the peer's, never re-seeked
// (net_display keeps it out of the report + kill paths). The zap sound is
// played by the GLGame handler that owns the audio volume model.
void Ship::net_receive_shock(const std::vector<Point> &pts) {
  if(pts.size() < 2) return;
  ShockBolt bolt(WrappedPoint(pts[0].x(), pts[0].y()), Point(1, 0), this);
  bolt.points = pts;
  bolt.growing = false;   // already grown: step_bolt only fades it now
  bolt.life = 1.0f;
  bolt.net_display = true;
  bolt.net_reported = true;
  shocks.push_back(std::move(bolt));
}

std::shared_ptr<int> Ship::net_start_missile_fly_loop() {
  if(missile_fly_sound == NULL) return nullptr;
  int ch = Mix_PlayChannel(-1, missile_fly_sound, -1);
  if(ch == -1) return nullptr;
  return std::shared_ptr<int>(new int(ch), [](int *p) { Mix_HaltChannel(*p); delete p; });
}

void Ship::set_shield_hum(bool on) {
  if(on) {
    // The distance gate applies only to STARTING the hum. It used to gate
    // the whole function, so a hum started at full volume could never be
    // halted once the ship drifted out of earshot (the host re-scales a
    // remote ship's sound_volume_scale by listener distance every tick) —
    // the loop played forever: Glenn's "hum stuck on".
    if(shield_hum_sound == NULL || sound_volume_scale < 1.0f) return;
    if(shield_hum_channel >= 0) return; // already playing, don't leak a new channel
    shield_hum_channel = Mix_PlayChannel(-1, shield_hum_sound, -1);
  } else if(shield_hum_channel >= 0) {
    Mix_HaltChannel(shield_hum_channel);
    shield_hum_channel = -1;
  }
}

void Ship::stop_god_mode_music() {
  if(god_mode_music_channel >= 0) {
    Mix_HaltChannel(god_mode_music_channel);
    god_mode_music_channel = -1;
  }
  god_mode_music_phase = 0;
}

void Ship::update_god_mode_music(int time_remaining) {
  if(sound_volume_scale < 1.0f) return;
  if(time_remaining <= 0) {
    stop_god_mode_music();
    return;
  }
  int target_phase = (time_remaining > 3000) ? 1 : 2;
  if(target_phase == god_mode_music_phase) return;  // already correct, nothing to do
  // Stop whatever is currently playing
  if(god_mode_music_channel >= 0) {
    Mix_HaltChannel(god_mode_music_channel);
    god_mode_music_channel = -1;
  }
  Mix_Chunk *chunk = (target_phase == 1) ? god_mode_music_sound : god_mode_music_warn_sound;
  if(chunk != NULL) {
    god_mode_music_channel = Mix_PlayChannel(-1, chunk, -1);
  }
  god_mode_music_phase = target_phase;
}

float Ship::heading() const {
  //FIX: shouldn't have to calculate this each time
  return facing.direction();
}

void Ship::rotate_left(bool on) {
  still_rotating_left = on;
  if(on) {
    rotation_direction = LEFT;
  } else if (still_rotating_right) {
    rotation_direction = RIGHT;
  } else {
    rotation_direction = NONE;
  }
  play_rotating_sound(on);
}

void Ship::rotate_right(bool on) {
  still_rotating_right = on;
  if(on) {
    rotation_direction = RIGHT;
  } else if (still_rotating_left) {
    rotation_direction = LEFT;
  } else {
    rotation_direction = NONE;
  }
  play_rotating_sound(on);
}

void Ship::play_rotating_sound(bool on) {
  if(boost_sound != NULL) {
    if(on && !thrusting && !reversing) {
      Mix_VolumeChunk(boost_sound, MIX_MAX_VOLUME/16);
    }
    if(!on) {
      if(thrusting || reversing) {
        Mix_VolumeChunk(boost_sound, MIX_MAX_VOLUME/8);
      } else {
        Mix_VolumeChunk(boost_sound, 0);
      }
    }
  }
}

WrappedPoint Ship::gun() const {
  return WrappedPoint(position + (facing * height * 1.05));
}

void Ship::mark_last_bullet_trail() {
  if(!bullets.empty())
    bullets.back().has_trail = true;
}

void Ship::mark_last_bullet_kills_invincible() {
  if(!bullets.empty())
    bullets.back().kills_invincible = true;
}
void Ship::mark_last_bullet_piercing() {
  if(!bullets.empty())
    bullets.back().piercing = true;
}

void Ship::fire_bullet_from_gun() {
  // PROTO 14: the host's remote ship mints no bullets — the weapon sim
  // still ran (cooldown/ammo/trigger bookkeeping stays approximately in
  // step with the client's), but the real bullet arrives as a MSG_SHOT
  // report and spawns via net_spawn_reported_bullet.
  if (net_remote_gun) return;
  if(shoot_sound != NULL && sound_volume_scale > 0.0f) {
    Mix_VolumeChunk(shoot_sound, (int)(MIX_MAX_VOLUME * sound_volume_scale));
    Mix_PlayChannel(-1, shoot_sound, 0);
  }
  net_shots.push_back(this);  // net host relays its player's shots
  bullets.push_back(Particle(gun(), facing * 0.615f + velocity * 0.99f, 2000.0f));
  mark_last_bullet_trail();
  mark_last_bullet_kills_invincible();
  net_report_last_bullet();
}

void Ship::net_report_last_bullet() {
  if (!net_report_shots || bullets.empty()) return;
  Particle &b = bullets.back();
  b.net_id = ++net_shot_seq;
  NetShotReport r;
  r.id = b.net_id;
  r.x = b.position.x(); r.y = b.position.y();
  r.vx = b.velocity.x(); r.vy = b.velocity.y();
  r.kills_invincible = b.kills_invincible;
  r.has_trail = b.has_trail;
  r.piercing = b.piercing;
  net_shot_reports.push_back(r);
}

// PROTO 14: spawn the remote player's reported shot as an exact clone —
// same spawn point, same velocity (spread already applied by the firing
// client), same identity for the MSG_HIT consume.
void Ship::net_spawn_reported_bullet(uint32_t id, const Point &pos,
                                     const Point &vel, bool kills_inv,
                                     bool trail, bool piercing) {
  // A piercing clone is a beam bolt — play the beam's own sound, not the
  // pew (the chunk is shared/cached; every play site sets volume first).
  Mix_Chunk *snd = shoot_sound;
  if (piercing) {
    static Mix_Chunk *beam_snd = Mix_LoadWAV(asset_path("audio/beam.wav").c_str());
    if (beam_snd) snd = beam_snd;
  }
  if(snd != NULL && sound_volume_scale > 0.0f) {
    Mix_VolumeChunk(snd, (int)(MIX_MAX_VOLUME * sound_volume_scale));
    Mix_PlayChannel(-1, snd, 0);
  }
  bullets.push_back(Particle(pos, vel, 2000.0f));
  Particle &b = bullets.back();
  b.net_id = id;
  b.kills_invincible = kills_inv;
  b.has_trail = trail;
  b.piercing = piercing;
  // 1 Hz proof-of-flow: reported clones spawning at all (the handler is
  // otherwise silent, and its failure mode would just be an idle gun).
  if (Net::net_debug_enabled()) {
    static Uint32 s_last = 0;
    static int s_count = 0;
    s_count++;
    Uint32 now = SDL_GetTicks();
    if (now - s_last >= 1000) {
      NET_LOG("net: %d reported shots/s spawned\n", s_count);
      s_last = now;
      s_count = 0;
    }
  }
}

WrappedPoint Ship::tail() const {
  return WrappedPoint(position - (facing * 15.0));
}

void Ship::puts() {
  cout << "Facing: " << facing;
  cout << " Position: " << position;
  cout << " Velocity: " << velocity;
  cout << endl;
}

void Ship::step(float delta, const Grid &grid) {
  toggled = !toggled;
  list<Behaviour *>::iterator vi = behaviours.begin();
  while(vi != behaviours.end()) {
    (*vi)->step(delta);
    if((*vi)->is_done()) {
      delete (*vi);
      vi = behaviours.erase(vi);
    } else {
      vi++;
    }
  }

  if(is_alive()) {
    if(invincible && god_mode_time_remaining() <= 0) {
      time_left_invincible -= delta;
      if(time_left_invincible < 0) {
        invincible = false;
        set_shield_hum(false);
      }
    }

    // Replay queued remote trigger presses (host side, PROTO 12 INPUT): a
    // semi-automatic primary fires once per press, so presses the client
    // batched into one INPUT delta (a lost-packet blackout straddling two
    // real fires) must arm the weapon once EACH across successive steps —
    // collapsing them into a single shoot(true) fired once while the client
    // decremented twice, and the ammo clamp then pinned the divergence.
    // Automatics consume the whole queue in one arm (extra presses are
    // meaningless while held). Local ships never queue (only the host's
    // INPUT handler feeds it).
    if(net_queued_shot_presses > 0 && !primary_weapons.empty()) {
      if((*primary)->is_automatic()) {
        shoot(true);                   // held-fire: one arm covers every press
        net_queued_shot_presses = 0;
      } else if(!(*primary)->is_shooting()) {
        shoot(true);                   // replay one press per step
        net_queued_shot_presses--;
      } else {
        // Still armed at drain time = a primary that never self-disarms
        // (god mode). Extra presses are meaningless while it holds, and
        // leaving them queued would block the release-disarm in the INPUT
        // handler (it waits for an empty queue). Semi-autos are never armed
        // here — they disarm in their own step(), after this drain.
        net_queued_shot_presses = 0;
      }
    }

    for(auto it = primary_weapons.begin(); it != primary_weapons.end(); ) {
      (*it)->step(delta);
      Weapon::GodMode *gm = dynamic_cast<Weapon::GodMode*>(*it);
      if(gm && gm->empty()) {
        // add_god_mode() recorded the weapon held at pickup time — hand the
        // trigger back to it, same policy as an exhausted primary. (The old
        // rule here took the list neighbour, ignoring the recorded history.)
        auto next = fallback_primary(it);
        if(it == primary) primary = next;
        delete *it;
        it = primary_weapons.erase(it);
      } else {
        ++it;
      }
    }
    for(auto it = secondary_weapons.begin(); it != secondary_weapons.end(); ++it) {
      if (!dynamic_cast<Weapon::Missile*>(*it))
        (*it)->step(delta);
    }

    // A lance fired this frame: execute the instantaneous pulse now that the
    // collision grid is in hand.
    if(lance_pulse_pending) {
      lance_pulse_pending = false;
      fire_lance_pulse(grid);
    }

    // God mode music: update phase transitions and play rapid tic beeps in last second
    int gm_time = god_mode_time_remaining();
    update_god_mode_music(gm_time);
    if(gm_time > 0 && gm_time <= 1000 && sound_volume_scale >= 1.0f && tic_sound != NULL) {
      int prev_gm_time = gm_time + (int)delta;
      if((prev_gm_time / 200) != (gm_time / 200)) {
        Mix_PlayChannel(-1, tic_sound, 0);
      }
    }

  } else if (lives > 0) {
    if(sound_volume_scale >= 1.0f) {
      if(floor((time_until_respawn-1)/1000) != floor((time_until_respawn-delta-1)/1000)) {
        if(time_until_respawn > 1000) {
          if(tic_sound != NULL) {
            Mix_PlayChannel(-1, tic_sound, 0);
          }
        } else {
          if(tic_low_sound != NULL) {
            Mix_PlayChannel(-1, tic_low_sound, 0);
          }
        }
      }
    }
    time_until_respawn -= delta;
    if(time_until_respawn < 0) {
      respawn(grid);
    }
  }

  facing.rotate(rotation_direction * rotation_force * rotation_scale / mass * delta);
  Point acceleration = Point(0,0);
  if(boosting) {
  	acceleration += ((facing * boost_force) / mass);
    temperature += boost_heat;
    explode();
    boosting = false;
  }
  if(thrusting) {
  	acceleration += ((facing * thrust_force * thrust_analog) / mass);
    temperature += heat_rate * delta;
	}
  if(reversing) {
  	acceleration += ((facing * reverse_force * reverse_analog) / mass);
    temperature += retro_heat_rate * delta;
	}
  temperature -= cool_rate * delta;
  if(temperature <= 0)
    temperature = 0;
  if(temperature > explode_temperature && alive) {
    kill();
    detonate();
  }

  velocity += acceleration * delta;
  if(is_alive()) {
    velocity = velocity - velocity * friction * delta;
  }
  CompositeObject::step(delta);

  for(size_t i = 0; i < bullets.size(); ) {
    bullets[i].step(delta);
    if(!bullets[i].is_alive()) {
      bullets[i] = std::move(bullets.back());
      bullets.pop_back();
    } else {
      if(bullets[i].has_trail) {
        bullets[i].trail_timer -= delta;
        if(bullets[i].trail_timer <= 0) {
          bullets[i].trail_timer = 50;
          Point spread((rand()%100-50)*0.0002f, (rand()%100-50)*0.0002f);
          bullet_trails.push_back(Particle(bullets[i].position, spread, 200.0f));
        }
      }
      ++i;
    }
  }

  for(size_t i = 0; i < lance_pulses.size(); ) {
    lance_pulses[i].time_left -= delta;
    if(lance_pulses[i].time_left <= 0.0f) {
      lance_pulses[i] = std::move(lance_pulses.back());
      lance_pulses.pop_back();
    } else {
      ++i;
    }
  }

  for(size_t i = 0; i < bullet_trails.size(); ) {
    bullet_trails[i].step(delta);
    if(!bullet_trails[i].is_alive()) {
      bullet_trails[i] = std::move(bullet_trails.back());
      bullet_trails.pop_back();
    } else {
      ++i;
    }
  }

  for(size_t i = 0; i < mines.size(); ) {
    mines[i].step(delta);
    if(!mines[i].is_alive()) {
      mines[i] = std::move(mines.back());
      mines.pop_back();
    } else {
      ++i;
    }
  }

  for(size_t i = 0; i < giga_mines.size(); ) {
    giga_mines[i].step(delta);
    if(!giga_mines[i].is_alive()) {
      giga_mines[i] = std::move(giga_mines.back());
      giga_mines.pop_back();
    } else {
      ++i;
    }
  }

  for(size_t i = 0; i < shockwaves.size(); ) {
    shockwaves[i].step(delta);
    if(!shockwaves[i].alive()) {
      shockwaves[i] = std::move(shockwaves.back());
      shockwaves.pop_back();
    } else {
      ++i;
    }
  }

  // Step shock bolts unconditionally so they finish arcing and fading regardless
  // of weapon state. A bolt is only removed once it has faded AND its struck list
  // has been drained (asteroids in collide_grid, hostiles in GLGame) so no hit is
  // dropped on the frame it dies.
  for(size_t i = 0; i < shocks.size(); ) {
    shocks[i].step_bolt(delta, missile_asteroids, shock_targets, &grid);
    // PROTO 22: once a bolt is done growing, report its exact polyline to the
    // peer so the remote flash uses identical segments (a re-seek would
    // diverge — grow_segment jitters with rand()). "Done growing" is either
    // reaching MAX_SEGMENTS or stop() halting it early at a survivor it
    // couldn't destroy — both clear `growing`, so key off !growing rather than
    // the growing→!growing edge (stop() flips it in the drain, AFTER this
    // check ran, so an edge test would miss stopped bolts entirely). The
    // net_reported guard keeps it to a single report. Display replicas
    // (net_display) are never re-reported.
    if(net_report_shots && !shocks[i].net_display && !shocks[i].net_reported &&
       !shocks[i].growing && shocks[i].points.size() >= 2) {
      net_shock_reports.push_back(shocks[i].points);
      shocks[i].net_reported = true;
    }
    if(!shocks[i].is_alive() && shocks[i].struck.empty()) {
      shocks[i] = std::move(shocks.back());
      shocks.pop_back();
    } else {
      ++i;
    }
  }

  // Step missiles unconditionally so they keep flying regardless of weapon state.
  for(size_t i = 0; i < missiles.size(); i++) {
    missiles[i].step_missile(delta, missile_asteroids, missile_ships_list,
                             missiles_seek_players);
  }

  // Missile movement is handled in Weapon::Missile::step() above.
  // Here we only handle expiry detonation.
  for(size_t i = 0; i < missiles.size(); ) {
    if(!missiles[i].is_alive()) {
      detonate(missiles[i].position, missiles[i].velocity, MISSILE_SHRAPNEL);
      if(missile_explode_sound != NULL) {
        Mix_PlayChannel(-1, missile_explode_sound, 0);
      }
      missiles[i] = std::move(missiles.back());
      missiles.pop_back();
    } else {
      ++i;
    }
  }
}
