#include "net_lobby.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "gl_compat.h"
#include "glgame.h"
#include "invites.h"
#include "presence.h"
#include "glstarfield.h"
#include "mat4.h"
#include "menu.h"
#include "net_identity.h"
#include "net_policy.h"
#include "net_session.h"
#include "net_signal.h"
#include "net_transport.h"
#include "preferences.h"
#include "steam_build.h"
#include "typer.h"
#include "view/overlay.h"
#include "view/tap_band.h"

#if defined(__ANDROID__)
// Soft-keyboard coverage backend (android_main.cpp). Declared at file scope,
// OUTSIDE the anonymous namespace below: an anonymous-namespace declaration
// gets internal linkage (even with `extern`), so it can never match a
// definition in another TU — the NDK link fails with an undefined
// (anonymous namespace)::android_keyboard_cover_fraction(). Plain C++
// linkage on both sides (a C-vs-C++ linkage mismatch across TUs is the
// other known NDK-link bite). The iOS twin lives inside the namespace
// unharmed: extern "C" names ignore namespaces for linkage.
float android_keyboard_cover_fraction();
#endif

namespace {

const int WORLD_W = 5000, WORLD_H = 5000;  // starfield extent (as in Menu)
const int STATUS_SHOW_MS = 4000;

// STUN-only in Milestone 1: if a paste-complete pair can't connect in this
// window they are almost certainly behind symmetric NATs (no relay yet).
const int CONNECT_TIMEOUT_MS = 25000;

// No word from the signal server in this window (room creation / join
// acknowledgement) — treat it as unreachable and use the manual flow.
const int SIGNAL_TIMEOUT_MS = 12000;

// The last room code THIS instance hosted (in-memory only). The host
// auto-copies its code to the clipboard, so after quitting a game the
// JOIN screen would otherwise auto-join the player's own dead room.
static std::string s_last_hosted_code;
// Rooms confirmed dead this run (host BYE / relay says gone). A list, not
// one slot: a typed bad code dead-marks too, and with a single slot it
// EVICTED the stale clipboard code marked moments earlier — freeing the
// repoll to probe it a second time (the engine of task #165's double
// attempt).
static std::vector<std::string> s_dead_codes;
static bool room_is_dead(const std::string &code) {
  for (size_t i = 0; i < s_dead_codes.size(); i++)
    if (s_dead_codes[i] == code) return true;
  return false;
}

// TURN test hook, process-wide: a "0" typed before the room code (0 is
// not in the code alphabet) toggles relay-only joins — the one-phone
// test (TESTING.md), where same-device peers otherwise always pick the
// direct ICE path. Process-wide, not per-lobby, so the mid-game
// AUTO-rejoin after a TURN-credential kick re-relays too (a direct
// reconnect would silently end the test).
static bool s_join_force_relay = false;

// Lobby tap bands — one definition each for the drawn label AND the
// touch hit-test (view/tap_band.h). The bottom RETURN TO MENU strip is
// the shared TapBand::return_to_menu.
// CodeEntry: the soft keyboard buries the bottom exit strip, so the way
// out lives top-right beside the hoisted header.
const TapBand kBackBand(0.85f, 480, 22, 6.0f, /*to_top=*/true, false, 0.72f);
// RoomHost: the "TAP TO SHARE" line, padded to finger height.
const TapBand kShareBand(0.5f, -80, 18, 42.0f);
// CodeEntry LAN host bands (touch): above the soft keyboard (max 3).
// Filled BOTTOM-UP — one host uses only the lowest band, sitting in
// the free space just above the keyboard, well clear of the code
// (Glenn's S25 screenshot); a full list grows upward toward the
// compressed slots. These are the BASE positions (lowest band bottoms
// out ~y 49); the real keyboard is measured at runtime and the stack
// lifts clear of it — see lan_band_lift below. Field results that
// killed the fixed layout: the S25's keyboard reaches ~y 48 (the
// "half the screen" assumption undershot it, and even the measured
// position overlapped slightly), and an iPhone landscape keyboard
// covers ~60% of the screen (~y 120), drowning the lowest band
// entirely.
const int kLanBandCount = 3;
const TapBand kLanBand[kLanBandCount] = {TapBand(0.5f, 225, 18, 12.0f),
                                         TapBand(0.5f, 155, 18, 12.0f),
                                         TapBand(0.5f, 85, 18, 12.0f)};
const float kLanBandSpacing = 70.0f;  // anchor-to-anchor in kLanBand

// Measured soft-keyboard coverage (fraction of the window height, 0 =
// hidden/unknown): UIKit keyboard-frame notifications on iOS
// (ios_keyboard.mm), the activity's visible-display-frame listener on
// Android (NewtoniaActivity → android_main.cpp). Fraction 0 reproduces
// the base layout exactly, so platforms without a backend are
// untouched.
#if defined(__IOS__)
extern "C" float ios_keyboard_cover_fraction(void);
static float soft_keyboard_fraction() { return ios_keyboard_cover_fraction(); }
#elif defined(__ANDROID__)
// Backend declared at file scope above this namespace (linkage — see there).
static float soft_keyboard_fraction() {
  return android_keyboard_cover_fraction();
}
#else
static float soft_keyboard_fraction() { return 0.0f; }
#endif

// How far the band stack must rise (Typer units) so the lowest band
// clears the measured keyboard. 0 with no (or a low-enough) keyboard.
// Fractions under 0.15 are ignored — a visible nav/gesture bar shrinks
// the Android visible frame a few percent without any keyboard up.
static float lan_band_lift() {
  float f = soft_keyboard_fraction();
  if (f < 0.15f) return 0.0f;
  const TapBand &low = kLanBand[kLanBandCount - 1];
  float band_bottom = (low.y - low.size) - (low.size + low.pad);
  float kb_top = (2.0f * f - 1.0f) * Typer::scaled_window_height;
  float lift = kb_top + 12.0f - band_bottom;
  return lift > 0.0f ? lift : 0.0f;
}

// How many bands fit between the lifted stack's bottom and the code
// slots. The draw side compresses the heading/slots upward whenever a
// lift is active (slots y 345 size 28 → glyphs reach y 289), so the
// ceiling here matches that squeezed layout.
static int lan_bands_fit(float lift) {
  if (lift <= 0.0f) return kLanBandCount;
  const TapBand &low = kLanBand[kLanBandCount - 1];
  float band_top0 = (low.y - low.size) + (low.size + low.pad) + lift;
  int fit = 1 + (int)((285.0f - band_top0) / kLanBandSpacing);
  if (fit < 1) fit = 1;
  if (fit > kLanBandCount) fit = kLanBandCount;
  return fit;
}

// The band at kLanBand[idx] raised by the active lift — ONE geometry
// feeding both draw and hit-test, preserving the TapBand invariant.
static TapBand lan_band_at(int idx, float lift) {
  const TapBand &b = kLanBand[idx];
  return TapBand(b.nx, b.y + lift, b.size, b.pad);
}

// CodeEntry controller picker: the code alphabet as a grid under the
// code slots (desktop layout — touch uses the soft keyboard instead).
// Non-Deck pads only — where the floating keyboard has actually shown,
// the keyboard replaces the grid entirely (Glenn). Two rows sit between
// the button-hint line at -48 (glyphs reach ~-84) and the LAN rows
// below (selected cells grow to size 22 → 44 tall, so the second row
// bottoms out around -248).
const int PICKER_COLS = 15;
const float PICKER_TOP_Y = -152.0f;
const float PICKER_ROW_H = 52.0f;
const float PICKER_CELL_W = 45.0f;

// Pull a room code out of clipboard text. The host auto-copies the universal
// join link (https://…/join?code=XXXXX) so a friend can paste a clickable
// link, but the same clipboard feeds this JOIN-screen auto-join — so accept
// either a link (take the valid code chars right after "code=") or a bare
// code (strip whitespace, uppercase, the legacy behaviour). Returns the
// uppercased code; the caller still validates length + alphabet.
static std::string room_code_from_clip(const std::string &text) {
  size_t p = text.find("code=");
  if (p != std::string::npos) {
    std::string code;
    for (size_t i = p + 5; i < text.size() && code.size() < (size_t)NET_ROOM_CODE_LEN; i++) {
      char c = text[i];
      if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
      if (!net_room_code_char_ok(c)) break;  // stop at '&', '#', whitespace, end
      code += c;
    }
    return code;
  }
  std::string code;
  for (size_t i = 0; i < text.size(); i++) {
    char c = text[i];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    code += c;
  }
  return code;
}

}  // namespace

void NetLobby::mark_room_dead(const std::string &code) {
  if (!code.empty() && !room_is_dead(code)) s_dead_codes.push_back(code);
}

NetLobby::NetLobby()
    : screen_(Choose),
      selection_(0),
      hosting_(false),
      transport_(nullptr),
      session_(nullptr),
      signal_(nullptr),
      offer_sent_(false),
      answer_sent_(false),
      signal_wait_ms_(0),
      paste_pending_(false),
      code_clip_pending_(false),
      code_clip_retry_ms_(0),
      rejoin_mode_(false),
      rejoin_retry_ms_(0),
      rejoin_budget_ms_(0),
      connect_wait_ms_(0),
      status_ms_(0),
      currentTime(0),
      viewpoint(Point(0, WORLD_H / 2)),
      starfield(new GLStarfield(Point(WORLD_W, WORLD_H), star_density_scale())) {
  // A controller plugged in before the lobby opened (Steam Deck: always)
  // gets the picker/hints immediately, not only after the first press.
  // Not on web: browsers report phantom gamepad slots (Chrome's
  // getGamepads() is four nulls with nothing connected), so SDL
  // over-counts there — a real pad reveals the picker on its first input.
#ifndef __EMSCRIPTEN__
  controller_seen_ = SDL_NumJoysticks() > 0;
#endif
  // Warm the platform verification credential (NETPLAY.md V1): minting a
  // Steam Web-API ticket is async, so kick it off the moment the lobby opens
  // — by the time a room is created/joined (socket + relay round-trip later)
  // the ticket has completed and rides the first identity announce. A no-op
  // on builds without a verification backend (returns "").
  (void)net_local_verify_credential();
}

void NetLobby::fail_online_not_allowed() {
  fail_headline_ = "ONLINE PLAY NOT ALLOWED";
  set_status("THIS ACCOUNT CANNOT PLAY ONLINE", 2 * STATUS_SHOW_MS);
  policy_blocked_ = true;
  screen_ = LobbyFailed;
}

NetLobby::NetLobby(const std::string &rejoin_code) : NetLobby() {
  hosting_ = false;
  // Same policy gate as the interactive JOIN commit (confirm()): this ctor
  // is a join commit too — auto-rejoin and platform invite accepts.
  if (!net_online_play_allowed()) {
    fail_online_not_allowed();
    return;
  }
  Presence::set_joining();  // auto-rejoin / invite-accept: joining a game
  Net::set_net_log_role(false);  // rejoin is always the client side
  transport_ = NetTransport::create();
  signal_ = NetSignal::create();
  if (!transport_ || !signal_) {
    set_status("NETPLAY NOT AVAILABLE ON THIS BUILD");
    screen_ = LobbyFailed;
    return;
  }
  code_entry_ = rejoin_code;
  rejoin_mode_ = true;
  // A backgrounded host that hasn't resumed within a minute usually isn't
  // coming back soon (the room's reclaim grace is 2 min, but waiting it
  // out reads as a hang — the joiner can TRY AGAIN from the fail screen).
  rejoin_budget_ms_ = 60000;
  transport_->set_trickle(true);  // room flow: live relay carries candidates
  transport_->set_force_relay(s_join_force_relay);  // keep a TURN test relayed
  signal_->connect_join(net_signal_url(), code_entry_);
  signal_wait_ms_ = 0;
  screen_ = RoomJoining;
  set_status("RECONNECTING");
}

// CodeEntry character picker for controllers: the code alphabet drawn as
// a grid under the code slots, current cell enlarged. Doubles as visible
// documentation of which characters codes can contain. Each row centres
// itself (the last row is short).
void NetLobby::draw_picker() {
  const int n = (int)strlen(NET_ROOM_CODE_ALPHABET);
  for (int i = 0; i < n; i++) {
    int row = i / PICKER_COLS, col = i % PICKER_COLS;
    int row_start = row * PICKER_COLS;
    int cols_in_row = (n - row_start < PICKER_COLS) ? n - row_start
                                                    : PICKER_COLS;
    float x = (col - (cols_in_row - 1) * 0.5f) * PICKER_CELL_W;
    float y = PICKER_TOP_Y - row * PICKER_ROW_H;
    char s[2] = {NET_ROOM_CODE_ALPHABET[i], '\0'};
    Typer::draw_centered(x, y, s, i == picker_index_ ? 22 : 13);
  }
}

// The soft keyboard drives CodeEntry on touch platforms; the existing
// keyboard() path receives its characters via SDL_TEXTINPUT. On Steam
// Deck the Steamworks floating keyboard plays the same role — it types
// plain key events into the game, summoned/dismissed on the same edges.
// Where neither exists this is a no-op and the controller picker or a
// physical keyboard carries code entry. Returns true when the floating
// keyboard actually came up (Deck) — the caller hides the picker under
// it (floating_kb_up_).
bool NetLobby::code_entry_keyboard(bool open) {
  if (is_touch_mode()) {
    if (open) SDL_StartTextInput();
    else SDL_StopTextInput();
    return false;
  }
  if (open) {
    // The code slots (virtual y 120 down to 24, size-48 glyphs, centre
    // half-width) as a window-pixel rect, so the keyboard docks clear
    // of what the player is typing into.
    float H = Typer::scaled_window_height;
    int top = (int)((1.0f - 120.0f / H) * Typer::window_height * 0.5f);
    int height = (int)((96.0f / H) * Typer::window_height * 0.5f);
    return steam_show_floating_keyboard(Typer::window_width / 4, top,
                                        Typer::window_width / 2, height);
  }
  // Count the dismiss only when the keyboard is believed up: it then
  // fires exactly one Dismissed callback, which the tick drain credits
  // to us rather than reading as a user dismiss (see the member doc).
  // The dismiss call itself is always issued — harmless if already down.
  if (floating_kb_up_) floating_kb_dismiss_pending_++;
  steam_dismiss_floating_keyboard();
  return false;
}

// LAN rejoin (round 4): the lost session came through the LAN door, so
// there is no room code — browse for the remembered host name and
// auto-pair when its beacon reappears (the host's game re-beacons on
// loss; a restarted host beacons the same name from its lobby). Shares
// the relay rejoin's honest-wait display and 60 s budget.
NetLobby::NetLobby(const std::string &lan_host_name, LanRejoinTag)
    : NetLobby() {
  hosting_ = false;
  Presence::set_joining();
  Net::set_net_log_role(false);
  transport_ = NetTransport::create();
  if (!transport_ || !NetLan::available() || !lan_browse_.start()) {
    set_status("NETPLAY NOT AVAILABLE ON THIS BUILD");
    screen_ = LobbyFailed;
    return;
  }
  lan_rejoin_ = true;
  lan_rejoin_name_ = lan_host_name;
  lan_browse_ms_ = 0;
  rejoin_mode_ = true;
  rejoin_budget_ms_ = 60000;
  screen_ = RoomJoining;
  set_status("RECONNECTING");
  NET_LOG("[lobby] lan rejoin: browsing for %s\n", lan_rejoin_name_.c_str());
}

// A rejoin attempt failed in a retryable way (network still down, relay
// briefly unreachable): stay on the joining screen and try again.
void NetLobby::schedule_rejoin_retry(const char *why, int delay_ms) {
  NET_LOG("[lobby] rejoin retry in %d ms (%s)\n", delay_ms, why);
  set_status("RECONNECTING");
  screen_ = RoomJoining;
  signal_wait_ms_ = 0;
  if (rejoin_retry_ms_ <= 0) rejoin_retry_ms_ = delay_ms;
}

// The handshake completed on a rejoin (WaitConnect Ready / Connected), then
// the fresh transport failed — a WebRTC path routinely flaps once in the
// first seconds after wifi/cell returns. Giving up here stranded the joiner
// on "CONNECTION LOST" with 20 s of budget and the host's 2 min reclaim grace
// still on the table (Glenn: wifi off 40 s, lost on re-enable). Drop the dead
// session and re-enter the retry loop (which is gated on !session_) so the
// next attempt reclaims the room within the remaining budget.
bool NetLobby::rejoin_retry_after_session_loss(const char *why) {
  if (!rejoin_mode_ || rejoin_budget_ms_ <= 0) return false;
  // LAN rejoin: the retry is a fresh browse, not a relay reconnect.
  if (lan_rejoin_) {
    connect_wait_ms_ = 0;
    lan_rejoin_restart(why);
    return true;
  }
  delete session_;
  session_ = nullptr;
  if (transport_) {
    transport_->close();
    delete transport_;
    transport_ = nullptr;
  }
  answer_sent_ = false;
  connect_wait_ms_ = 0;
  schedule_rejoin_retry(why, 1000);
  return true;
}

NetLobby::~NetLobby() {
  // Leaving the lobby (into the game once the peer connected, or back to the
  // menu): no longer joinable — the co-op slot is full or gone. A no-op if we
  // never advertised (joiner, or non-Steam build).
  Invites::clear_joinable();
  lan_teardown();  // beacon/browse sockets + the LAN door's transport
  delete session_;  // closes + deletes the transport it owns
  if (!session_ && transport_) {
    transport_->close();
    delete transport_;
  }
  if (signal_) {
    signal_->close();
    delete signal_;
  }
  // Cancel any verification ticket still outstanding (the one warmed on open
  // if we never joined, or a spent one) — but ONLY when this lobby ends the
  // netplay chain (backed out / failed to the menu). On a hand-off the GLGame
  // owns the credential lifetime: a host-reclaim re-attest needs the warmed
  // ticket (releasing here shipped every reclaim announce credential-less),
  // and on a fast ICE connect the hand-off can beat the worker's validation
  // round-trip, so cancelling here could invalidate a ticket still in
  // flight. ~GLGame mops up at the true end of the chain.
  if (!handed_off_to_game_) net_release_verify_credentials();
  delete starfield;  // owned; heap + GPU buffers leak per lobby visit otherwise
}

void NetLobby::set_status(const char *text, int show_ms) {
  status_ = text;
  status_ms_ = show_ms > 0 ? show_ms : STATUS_SHOW_MS;
}

void NetLobby::reset_to_choose() {
  fail_headline_ = "SOMETHING WENT WRONG";
  delete session_;
  session_ = nullptr;
  if (transport_) {
    transport_->close();
    delete transport_;
    transport_ = nullptr;
  }
  if (signal_) {
    signal_->close();
    delete signal_;
    signal_ = nullptr;
  }
  room_code_.clear();
  code_entry_.clear();
  offer_sent_ = false;
  answer_sent_ = false;
  signal_wait_ms_ = 0;
  paste_pending_ = false;
  connect_wait_ms_ = 0;
  rejoin_mode_ = false;  // a manual retry is a fresh join, not a rejoin
  rejoin_retry_ms_ = 0;
  lan_teardown();  // re-picking HOST/JOIN re-opens the LAN door fresh
  lan_host_name_.clear();
  screen_ = Choose;
}

void NetLobby::leave_to_menu() {
  // The hand-off to GLGame is committed once handed_off_to_game_ is set:
  // an input landing in the one-frame window before the StateManager
  // swap would overwrite next_state (request_state_change reassigns
  // without deleting), leaking the constructed game with its live
  // session — and, for a host, send_close would kill the room the game
  // is about to own. Ignore the exit; the swap happens next tick.
  if (handed_off_to_game_) return;
  code_entry_keyboard(false);
  floating_kb_up_ = false;
  // Host abandoning the room: kill it at the relay now, or its code stays
  // claimable-but-hostless for the whole reclaim grace window.
  if (hosting_ && signal_) signal_->send_close();
  request_state_change(new Menu());
}

void NetLobby::confirm() {
  if (screen_ == Choose) {
    // Platform policy gate (net_policy.h; the default backend always
    // allows). The menu already hides ONLINE when disallowed — this covers
    // a mid-session privilege change and any path straight into the lobby.
    if (!net_online_play_allowed()) {
      fail_online_not_allowed();
      return;
    }
    hosting_ = (selection_ == 0);
    // Every NET_LOG from here on says which side it came from —
    // side-by-side host+client captures otherwise read as one soup.
    Net::set_net_log_role(hosting_);
    transport_ = NetTransport::create();
    if (!transport_) {  // menu hides ONLINE when unavailable; belt & braces
      set_status("NETPLAY NOT AVAILABLE ON THIS BUILD");
      screen_ = LobbyFailed;
      return;
    }
    signal_ = NetSignal::create();
    // Room flow trickles ICE (M3-2b): the relay carries candidates, so the
    // SDP goes out the moment it exists instead of after gathering. The
    // manual clipboard flow keeps the full non-trickle blob (and
    // fall_back_to_manual() flips this off again before any start_*).
    if (signal_) transport_->set_trickle(true);
    if (hosting_) {
      Presence::set_hosting();  // "Hosting a Co-Op Game" in the friends list
      // LAN door (NETPLAY.md "LAN is not a mode"): beacon + serve the
      // manual INVITE blob over TCP from its own no-STUN transport,
      // beside the relay flow. Offline this is what still works; online
      // it's the couch shortcut. The lan_visible pref (INI-only) opts a
      // host out of the hostname broadcast entirely.
      lan_beacon_name_ = NetLan::local_host_name();
      if (g_prefs.lan_visible && NetLan::available() &&
          lan_announce_.start(lan_beacon_name_)) {
        lan_transport_ = NetTransport::create();
        if (lan_transport_) {
          lan_transport_->set_lan_only(true);
          lan_transport_->set_trickle(false);
          lan_transport_->start_host();
        }
      }
      if (signal_) {
        // start_host() waits for the Room frame: the relay sends the TURN
        // credentials first, and ICE servers bind at pc creation.
        signal_->connect_host(net_signal_url());
        signal_wait_ms_ = 0;
        screen_ = RoomHost;
      } else {
        transport_->start_host();
        screen_ = HostGathering;
      }
    } else {
      Presence::set_joining();  // "Joining a Co-Op Game"
      if (NetLan::available()) {
        lan_browse_.start();
        lan_browse_ms_ = 0;
        lan_blob_held_ = false;
      }
      if (signal_) {
        screen_ = CodeEntry;
        floating_kb_up_ = code_entry_keyboard(true);
        if (floating_kb_up_) floating_kb_available_ = true;
        // If the clipboard already holds the friend's code (the host side
        // auto-copies it), prefill and join without any typing. Started
        // here so the web backend's read stays inside the user gesture.
        code_clip_pending_ = true;
        net_clipboard_read_start();
      } else {
        screen_ = JoinWaitOffer;
      }
    }
  } else if (screen_ == CodeEntry) {
    if (code_entry_.size() == (size_t)NET_ROOM_CODE_LEN) {
      code_entry_keyboard(false);
      floating_kb_up_ = false;
      // Every join through here is player-driven; the clipboard AUTO-join
      // site re-flags it right after this call (its failure stays silent —
      // task #165). The join itself is sent synchronously below, the
      // relay's verdict only ever arrives later via pump_signal.
      last_join_was_auto_ = false;
      // Applied here, not at transport creation: the "0" arming happens
      // while typing, after the transport already exists. The pc (and its
      // ICE policy) is only built at start_join, so this is in time.
      if (transport_) transport_->set_force_relay(s_join_force_relay);
      NET_LOG("[lobby] joining room %s\n", code_entry_.c_str());
      signal_->connect_join(net_signal_url(), code_entry_);
      signal_wait_ms_ = 0;
      screen_ = RoomJoining;
    } else {
      set_status("THE CODE IS 5 LETTERS");
    }
  } else if (lan_rejoin_browsing() && lan_sel_ >= 0) {
    // Enter/A on a highlighted row of the rejoin wait screen (see draw's
    // RoomJoining case): join it instead of waiting out the budget.
    lan_join_selected();
  } else if (screen_ == LobbyFailed) {
    // A policy-blocked account gets no chooser: HOST and JOIN would both
    // refuse with the same headline, an infinite dead-end — back to the
    // menu, where show_online_row() hides ONLINE for the same reason.
    if (policy_blocked_) {
      leave_to_menu();
      return;
    }
    // Back to the HOST/JOIN chooser for everyone (Glenn). A failed JOIN
    // used to rebuild an empty join screen via retry_join(), but that
    // screen deliberately skips the clipboard auto-read — re-entering
    // JOIN from the chooser is one press and arms everything fresh.
    reset_to_choose();
  }
}

// The signal server never answered (or refused the room): keep the pair
// playable through the Milestone 1 clipboard flow.
void NetLobby::fall_back_to_manual(const char *why) {
  NET_LOG("[lobby] manual fallback: %s\n", why);
  if (signal_) {
    signal_->close();
    delete signal_;
    signal_ = nullptr;
  }
  // No worker: this is a worker-less (OFFLINE) session — the peer's claimed
  // name/platform render as-is (net_identity.h), and nothing is attested.
  used_worker_ = false;
  attested_peer_ = NetIdentity();
  set_status("NO ROOM SERVER - USING MANUAL CODES");
  // The clipboard blob must carry every candidate: back to non-trickle
  // (always before start_* — the host deferred its start to the Room
  // frame that never came, the joiner starts at paste time).
  if (transport_) transport_->set_trickle(false);
  if (hosting_ && transport_) transport_->start_host();  // was deferred
  screen_ = hosting_ ? HostGathering : JoinWaitOffer;
}

// Close the LAN door: the co-op slot is spoken for (either door's joiner
// completed, or the lobby is being torn down/reset).
void NetLobby::lan_teardown() {
  lan_announce_.stop();
  lan_browse_.stop();
  if (lan_transport_) {
    lan_transport_->close();
    delete lan_transport_;
    lan_transport_ = nullptr;
  }
  lan_offer_set_ = false;
  lan_joining_ = false;
  lan_sel_ = -1;
  lan_browse_ms_ = 0;
  lan_blob_held_ = false;
}

// Host side of the LAN door, every tick while hosting: feed the announcer
// the offer blob once gathering mints it, and adopt a completed joiner —
// the LAN transport becomes THE session transport and the relay door
// closes (its room is killed so the code dies with it).
void NetLobby::lan_host_update(int delta) {
  if (!lan_announce_.running()) return;
  if (session_) {  // the relay door won while we were beaconing
    lan_teardown();
    return;
  }
  // Follow a name change while nobody is engaged (the iOS Game Center
  // alias resolves after the door opened when hosting starts fast, or
  // seconds into a restarted app whose dropped client is browsing for
  // the alias): fresh joiners see the current name instead of a stale
  // generic one, and the rejoin-by-name window converges. From pairing
  // on the name is frozen for the session — the hand-off below copies
  // lan_beacon_name_ to the game, whose loss re-beacon repeats it
  // verbatim, so the name a client tapped and remembered can't drift.
  if (!lan_announce_.peer_engaged()) {
    std::string name = NetLan::local_host_name();
    if (name != lan_beacon_name_) {
      lan_beacon_name_ = name;
      lan_announce_.set_host_name(name);
    }
  }
  if (!lan_offer_set_ && lan_transport_ &&
      lan_transport_->local_description_ready()) {
    lan_announce_.set_offer_blob(
        Net::encode_signal(true, lan_transport_->local_description()));
    lan_offer_set_ = true;
  }
  std::string answer_blob;
  if (!lan_announce_.update(delta, answer_blob)) return;

  std::string sdp;
  if (Net::decode_signal(answer_blob, sdp) != 'A' || !lan_transport_) {
    NET_LOG("[lobby] lan answer blob invalid - ignoring\n");
    return;
  }
  NET_LOG("[lobby] lan joiner completed - adopting the lan transport\n");
  // The room stays OPEN (it used to be killed here): the WaitConnect
  // handoff gives it to GLGame via net_adopt_signal exactly like a
  // relay pairing, so a LAN session keeps the relay session's whole
  // loss toolkit — rejoin by code from anywhere, the Steam invite
  // re-advertise, and the fresh relay re-offer beside the LAN
  // re-beacon. Costs one idle socket; the code stays on the host's
  // screen for anyone who needs it. (The stored lobby offer goes stale
  // with the transport below — the rejoin poll re-offers fresh.)
  if (signal_)
    NET_LOG("[lobby] lan door won - keeping room %s open\n",
            room_code_.c_str());
  if (transport_) {
    transport_->close();
    delete transport_;
    transport_ = nullptr;
  }
  lan_announce_.stop();
  lan_transport_->set_remote_answer(sdp);
  session_ = new NetSession(lan_transport_, NetSession::HostRole);
  lan_transport_ = nullptr;  // owned by the session now
  // Identity display context follows HOW THE PEER WAS PAIRED — through
  // the local beacon door, i.e. net_identity.h's offline carve-out (the
  // claimed name/platform render as-is, like the manual-clipboard flow).
  // The relay room kept open above is a rejoin door, not what paired
  // this peer, and used_worker_ may be stale-true from it; a worker
  // attestation that arrives later still upgrades the fields. Field-hit
  // 2026-07-24: the Android host suppressed the iOS peer's claim to
  // role labels because the open room left the session ONLINE-strict.
  used_worker_ = false;
  attested_peer_ = NetIdentity();
  screen_ = WaitConnect;
  connect_wait_ms_ = 0;
}

// Joiner side: pump discovery (and a committed exchange). The offer blob
// arriving runs the exact manual-join machinery with the socket as the
// clipboard; the answer goes back in tick's JoinGathering case.
void NetLobby::lan_join_update(int delta) {
  if (lan_browse_.running()) lan_browse_ms_ += delta;
  lan_browse_.update(delta);

  // Never let a vanished row silently shift the highlight onto a
  // different host (the mis-tap class of bug) — drop to the code field.
  if (lan_sel_ >= lan_rows_shown()) lan_sel_ = -1;

  // LAN rejoin auto-select: the moment the remembered host's beacon is
  // back (and speaks our protocol), run the join as if its row was
  // picked. A connect refusal just waits for the next beacon; the
  // shared rejoin budget bounds the whole wait.
  if (lan_rejoin_ && !lan_joining_ && !session_ && screen_ == RoomJoining) {
    const std::vector<NetLan::HostInfo> &hosts = lan_browse_.hosts();
    for (size_t i = 0; i < hosts.size(); i++) {
      if (hosts[i].name == lan_rejoin_name_ &&
          hosts[i].proto == Net::PROTO_VERSION) {
        NET_LOG("[lobby] lan rejoin: %s reappeared\n",
                lan_rejoin_name_.c_str());
        lan_sel_ = (int)i;
        lan_join_selected();
        break;
      }
    }
  }

  if (!lan_joining_) return;

  std::string offer_blob;
  if (lan_browse_.offer_ready(offer_blob)) {
    std::string sdp;
    if (Net::decode_signal(offer_blob, sdp) == 'O' && transport_) {
      // The relay is out of the picture from here: LAN pairing is the
      // manual flow (non-trickle, and no STUN — host candidates only).
      if (signal_) {
        signal_->close();
        delete signal_;
        signal_ = nullptr;
      }
      // Worker-less pairing → offline identity context (net_identity.h):
      // the host's claimed name/platform render as-is. used_worker_ may
      // be stale-true from an earlier relay attempt in this SAME lobby
      // visit — field-hit 2026-07-24 (iOS): the own-old-room clipboard
      // probe joined the worker, failed, and its leftover flag made the
      // following LAN join render the Android host as a bare role label.
      used_worker_ = false;
      attested_peer_ = NetIdentity();
      transport_->set_trickle(false);
      transport_->set_lan_only(true);
      transport_->start_join(Net::strip_ice_candidates(sdp));
      screen_ = JoinGathering;
    } else {
      NET_LOG("[lobby] lan offer blob invalid\n");
      lan_joining_ = false;
      if (lan_rejoin_) {
        lan_rejoin_restart("invalid blob");
      } else {
        fail_headline_ = "COULD NOT JOIN THE LAN GAME";
        screen_ = LobbyFailed;
      }
    }
  }

  if (lan_browse_.failed() && screen_ != LobbyFailed) {
    lan_joining_ = false;
    // Rejoin mode keeps trying — the host may still be reopening its
    // door — bounded by the shared budget. A first join fails honestly.
    if (lan_rejoin_) {
      lan_rejoin_restart("exchange failed");
    } else {
      fail_headline_ = "COULD NOT JOIN THE LAN GAME";
      set_status("THE HOST DID NOT RESPOND");
      screen_ = LobbyFailed;
    }
  }
}

// One failed LAN rejoin attempt: back to browsing with fresh sockets
// (and a fresh transport if the old one was consumed by a start_join).
// The shared rejoin budget keeps ticking, so this can't loop forever.
void NetLobby::lan_rejoin_restart(const char *why) {
  NET_LOG("[lobby] lan rejoin retry (%s)\n", why);
  delete session_;
  session_ = nullptr;
  if (transport_) {
    transport_->close();
    delete transport_;
  }
  transport_ = NetTransport::create();
  lan_joining_ = false;
  lan_sel_ = -1;
  lan_browse_.start();
  lan_browse_ms_ = 0;
  screen_ = RoomJoining;
  set_status("RECONNECTING");
}

// The LAN rejoin wait screen is showing its live host rows (see draw's
// RoomJoining case) and nothing is committed yet — rows are selectable.
bool NetLobby::lan_rejoin_browsing() const {
  return screen_ == RoomJoining && lan_rejoin_ && !lan_joining_ && !session_;
}

// A host row on CodeEntry was chosen (Enter on the highlight).
void NetLobby::lan_join_selected() {
  const std::vector<NetLan::HostInfo> &hosts = lan_browse_.hosts();
  if (lan_sel_ < 0 || lan_sel_ >= (int)hosts.size()) return;
  if (hosts[lan_sel_].proto != Net::PROTO_VERSION) {
    set_status("DIFFERENT GAME VERSION - UPDATE BOTH GAMES");
    return;
  }
  if (!lan_browse_.connect_host(lan_sel_)) {
    set_status("COULD NOT REACH THAT HOST");
    return;
  }
  lan_host_name_ = hosts[lan_sel_].name;
  lan_joining_ = true;
  code_entry_keyboard(false);
  floating_kb_up_ = false;
  screen_ = RoomJoining;
  join_wait_ms_ = 0;
}

// The JOINER's signal path died before the room answered. The host in
// this spot falls back to minting a manual invite — useful, visibly a
// different mode. A joiner has nothing it can do by itself: the old
// behavior (silently landing on the manual paste screen) read as a
// normal join step that never finishes (Glenn, wifi off: "the client
// still sits on a normal join screen"). Fail honestly instead — TRY
// AGAIN gives a fresh join screen, and a manual invite from the host
// can be pasted right there (see the CodeEntry clipboard handler).
void NetLobby::join_unreachable(const char *why) {
  NET_LOG("[lobby] join failed: %s\n", why);
  if (signal_) {
    signal_->close();
    delete signal_;
    signal_ = nullptr;
  }
  fail_headline_ = "COULD NOT REACH THE ROOM SERVER";
  set_status("CHECK YOUR INTERNET CONNECTION", 2 * STATUS_SHOW_MS);
  screen_ = LobbyFailed;
}

void NetLobby::start_paste() {
  if (screen_ != HostWaitAnswer && screen_ != JoinWaitOffer) return;
  if (paste_pending_) return;
  net_clipboard_read_start();  // inside the key gesture (web requirement)
  paste_pending_ = true;
}

void NetLobby::handle_paste(const std::string &blob) {
  std::string sdp;
  char kind = Net::decode_signal(blob, sdp);
  NET_LOG("[lobby] pasted %d chars, kind=%c, screen=%d\n",
         (int)blob.size(), kind ? kind : '?', (int)screen_);
  if (screen_ == JoinWaitOffer) {
    if (kind == 'O') {
      // Candidate-free offer: keeps the joiner's ICE idle (nothing to time
      // out on) until the host pastes the reply and starts the checks —
      // see Net::strip_ice_candidates.
      transport_->start_join(Net::strip_ice_candidates(sdp));
      screen_ = JoinGathering;
    } else if (kind == 'A') {
      set_status("THAT IS AN ANSWER - PASTE THE HOST'S OFFER");
    } else {
      set_status("CLIPBOARD HAS NO NEWTONIA CODE");
    }
  } else if (screen_ == HostWaitAnswer) {
    if (kind == 'A') {
      transport_->set_remote_answer(sdp);
      screen_ = WaitConnect;
      connect_wait_ms_ = 0;
      session_ = new NetSession(transport_, NetSession::HostRole);
      transport_ = nullptr;  // owned by the session now
    } else if (kind == 'O') {
      set_status("THAT IS AN OFFER - PASTE YOUR FRIEND'S ANSWER");
    } else {
      set_status("CLIPBOARD HAS NO NEWTONIA CODE");
    }
  }
}

void NetLobby::copy_local_description() {
  std::string sdp;
  if (session_ && session_->transport())
    sdp = session_->transport()->local_description();
  else if (transport_)
    sdp = transport_->local_description();
  if (sdp.empty()) return;
  std::string blob = Net::encode_signal(hosting_, sdp);
  net_clipboard_write(blob);
  NET_LOG("[lobby] copied %s code to clipboard (%d chars)\n",
         hosting_ ? "invite" : "reply", (int)blob.size());
  set_status("COPIED TO CLIPBOARD");
}

// Announce our identity to the signaling worker (NETPLAY.md V0/V1): the
// claimed platform + display name, plus the platform verification credential
// (Steam Web-API ticket, "" otherwise) for the worker to attest. Sent when a
// socket comes up (Room/Joined) — the worker verifies and broadcasts the
// result to the peer.
void NetLobby::send_local_identity() {
  if (!signal_) return;
  const NetIdentity &me = net_local_identity();
  signal_->send_identity(me.platform, me.name, net_local_verify_credential());
}

// Consume the worker's `identity` broadcast describing the peer. Only a
// verified attestation is stored (promoted to ATTESTED and folded into the
// game at hand-off); an unverified broadcast leaves the peer role-labelled
// online. The name is re-sanitized locally, as every name-bearing path is.
void NetLobby::apply_peer_attestation(uint8_t platform, const std::string &name,
                                      bool verified) {
  if (!verified) return;
  attested_peer_.platform = platform;
  attested_peer_.platform_trust = NET_TRUST_ATTESTED;
  std::string clean = net_sanitize_name(name);
  attested_peer_.name = clean;
  attested_peer_.name_trust =
      clean.empty() ? NET_TRUST_ABSENT : NET_TRUST_ATTESTED;
  NET_LOG("net: identity attested name='%s' platform=%s(%u)\n",
          clean.c_str(), net_platform_label(platform), (unsigned)platform);
}

// Drives the room-code flow: pushes our SDP into the room when ready and
// reacts to relay events. No-op in the manual fallback (signal_ == null).
void NetLobby::pump_signal(int delta) {
  if (!signal_) return;

  // Host: the offer goes to the room the moment gathering yields it; the
  // worker replays it to a joiner arriving later.
  if (hosting_ && !offer_sent_ && !room_code_.empty() && transport_ &&
      transport_->local_description_ready()) {
    signal_->send_offer(transport_->local_description());
    offer_sent_ = true;
    NET_LOG("[lobby] offer sent to room %s\n", room_code_.c_str());
  }

  // Joiner: answer ready -> push it and start waiting on the transport.
  if (!hosting_ && !answer_sent_ && screen_ == RoomJoining && transport_ &&
      transport_->local_description_ready()) {
    signal_->send_answer(transport_->local_description());
    answer_sent_ = true;
    NET_LOG("[lobby] answer sent to room %s\n", room_code_.c_str());
    session_ = new NetSession(transport_, NetSession::ClientRole);
    transport_ = nullptr;
    connect_wait_ms_ = 0;
    screen_ = WaitConnect;
  }

  NetSignal::Event ev;
  while (signal_->poll(ev)) {
    switch (ev.kind) {
      case NetSignal::Event::Ice:
        ice_servers_.push_back(ev.text);
        break;
      case NetSignal::Event::Cand: {
        // Trickle ICE: feed the peer's candidate into whichever transport
        // is live (the lobby's own, or the session's after hand-off).
        NetTransport *t =
            transport_ ? transport_ : (session_ ? session_->transport() : nullptr);
        NET_LOG("[lobby] cand in (%s): %s\n", t ? "applied" : "DROPPED",
               ev.text.substr(0, 40).c_str());
        if (t) t->add_remote_candidate(ev.text2, ev.text);
        break;
      }
      case NetSignal::Event::Room:
        room_code_ = ev.text;
        room_token_ = ev.text2;  // reclaim proof, handed to the game
        used_worker_ = true;     // a worker is in the session (ONLINE-strict)
        send_local_identity();   // announce ourselves for the worker to attest
        s_last_hosted_code = room_code_;  // never auto-join our own room
        // Persist it too: a killed-and-relaunched app loses the static, and
        // its own code is still on the clipboard — the auto-join would walk
        // the ex-host straight into its own dead room.
        g_prefs.last_hosted_code = room_code_;
        save_preferences();
        // Copy the universal join link, not the bare code: on desktop/Steam
        // there is no OS share sheet, so the clipboard IS the host's only
        // way to hand a friend something clickable. The joiner's clipboard
        // auto-join (room_code_from_clip) still extracts the code from it,
        // and the zombie-room guards below compare that extracted code.
        net_clipboard_write(net_join_url(room_code_));
        // Advertise the room to the platform invite system (Steam "Join
        // Game" etc.). Cleared in the destructor once the slot fills or the
        // host leaves. The platform only ferries this code — the connection
        // still runs over signaling + WebRTC.
        Invites::set_joinable(room_code_);
        NET_LOG("[lobby] room %s (%d turn servers)\n", room_code_.c_str(),
               (int)ice_servers_.size());
        if (transport_) {
          transport_->set_ice_servers(ice_servers_);
          transport_->start_host();  // deferred from confirm()
        }
        break;
      case NetSignal::Event::Joined:
        room_code_ = code_entry_;  // acked — also disarms the timeout below
        used_worker_ = true;       // a worker is in the session (ONLINE-strict)
        send_local_identity();     // announce ourselves for the worker to attest
        set_status("ROOM FOUND - WAITING FOR THE HOST");
        break;
      case NetSignal::Event::Identity:
        // Worker peer attestation (NETPLAY.md V0): fold it in for the badge
        // and hand it to the game at construction.
        apply_peer_attestation(ev.platform, ev.text, ev.verified);
        break;
      case NetSignal::Event::PeerJoin:
        set_status("PLAYER 2 IS CONNECTING");
        break;
      case NetSignal::Event::PeerLeave:
        set_status("PLAYER 2 LEFT THE ROOM");
        // The departed joiner's attestation leaves with them (the worker
        // nulls its copy in drop_joiner for the same reason): a DIFFERENT
        // player can take the slot, and if their own verify fails or their
        // platform has no verifier, a kept attested_peer_ would be folded
        // onto them at hand-off — the replacement would wear the previous
        // joiner's attested name and badge for the whole game. The new
        // joiner's announce re-attests through the worker as usual.
        attested_peer_ = NetIdentity();
        // The room drops its stored offer with the joiner; put ours back
        // so the next joiner gets it replayed.
        if (hosting_ && transport_ && transport_->local_description_ready())
          signal_->send_offer(transport_->local_description());
        break;
      case NetSignal::Event::Offer:
        // Live relay: both ends are online, so the full offer applies and
        // ICE starts on both sides at once (no strip_ice_candidates — that
        // trick belongs to the clipboard flow's human-latency gap).
        if (!hosting_ && screen_ == RoomJoining && transport_) {
          // Version gate BEFORE ICE: an old build's connection recipe
          // may differ (pre-trickle SDP, no TURN), so the transports
          // would never connect and the session-level HELLO check could
          // never run — the player would just see an ICE timeout. The
          // offer carries the host's PROTO_VERSION (empty = old build).
          if (ev.text2 != std::to_string((int)Net::PROTO_VERSION)) {
            NET_LOG("[lobby] offer pv '%s' != ours %d - version mismatch\n",
                   ev.text2.c_str(), (int)Net::PROTO_VERSION);
            fail_headline_ = "VERSION MISMATCH";
            set_status("UPDATE BOTH GAMES", 2 * STATUS_SHOW_MS);
            screen_ = LobbyFailed;
            break;
          }
          NET_LOG("[lobby] joining with %d turn servers\n",
                 (int)ice_servers_.size());
          transport_->set_ice_servers(ice_servers_);
          transport_->start_join(ev.text);
        }
        break;
      case NetSignal::Event::Answer:
        if (hosting_ && transport_) {
          // Version gate (see the Offer case): discard a mismatched
          // answer and keep the room open — the outdated joiner fails
          // on its own side (its 45 s timeout, or its own pv check).
          if (ev.text2 != std::to_string((int)Net::PROTO_VERSION)) {
            NET_LOG("[lobby] answer pv '%s' != ours %d - ignored\n",
                   ev.text2.c_str(), (int)Net::PROTO_VERSION);
            set_status("PLAYER 2 HAS A DIFFERENT VERSION");
            break;
          }
          transport_->set_remote_answer(ev.text);
          session_ = new NetSession(transport_, NetSession::HostRole);
          transport_ = nullptr;
          connect_wait_ms_ = 0;
          screen_ = WaitConnect;
        }
        break;
      case NetSignal::Event::Error:
        if (rejoin_mode_ && (screen_ == RoomJoining || screen_ == CodeEntry)) {
          if (ev.text == "rate-limited") {
            schedule_rejoin_retry("rate-limited", 15000);
          } else if (ev.text == "host-closed") {
            // The host deliberately shut the room down — say so.
            mark_room_dead(code_entry_);
            fail_headline_ = "SERVER SHUT DOWN";
            set_status("THE HOST ENDED THE GAME");
            screen_ = LobbyFailed;
          } else {
            // no-such-room / expired / room-full: the room is genuinely
            // gone (host quit or grace elapsed) — retrying cannot help.
            mark_room_dead(code_entry_);
            set_status("THE ROOM IS GONE - HOST A NEW ONE");
            screen_ = LobbyFailed;
          }
          break;
        }
        if (screen_ == RoomJoining || screen_ == CodeEntry) {
          NET_LOG("[lobby] relay err '%s' (screen %d)\n", ev.text.c_str(),
                  (int)screen_);
          // A failed clipboard AUTO-join stays silent: the player never
          // asked for it, so its error reads as the lobby retrying — a
          // typed bad code was followed ~800 ms later by the repoll
          // probing a stale clipboard code, showing "NO ROOM WITH THAT
          // CODE" twice (Glenn, Deck, task #165). Dead-marking below
          // still stops further probes of that code.
          if (!last_join_was_auto_) {
            if (ev.text == "no-such-room") set_status("NO ROOM WITH THAT CODE");
            else if (ev.text == "room-full") set_status("THAT ROOM IS FULL");
            // The worker's fixed window is 10 min from the FIRST try, so
            // "a minute" oversold it (Glenn hit it while field-testing).
            else if (ev.text == "rate-limited") set_status("TOO MANY TRIES - WAIT A FEW MINUTES");
            else if (ev.text == "host-closed") set_status("THAT SERVER WAS SHUT DOWN");
            else set_status("THE ROOM HAS EXPIRED");
          }
          last_join_was_auto_ = false;
          // Any of these except rate-limited means the code is dead — stop
          // the clipboard auto-join from walking back into it.
          if (ev.text != "rate-limited") mark_room_dead(code_entry_);
          room_code_.clear();
          code_entry_.clear();
          answer_sent_ = false;
          screen_ = CodeEntry;
          floating_kb_up_ = code_entry_keyboard(true);
          if (floating_kb_up_) floating_kb_available_ = true;
        } else if (screen_ == RoomHost) {
          // fall_back_to_manual deletes signal_ — the poll loop must stop.
          fall_back_to_manual(ev.text.c_str());
          return;
        }
        break;
      case NetSignal::Event::Closed:
        if (screen_ == RoomHost && room_code_.empty()) {
          fall_back_to_manual("socket closed before room");
          return;
        } else if (screen_ == RoomJoining && room_code_.empty()) {
          if (rejoin_mode_) {
            schedule_rejoin_retry("socket closed", 5000);
            break;
          }
          // join_unreachable deletes signal_ — the poll loop must stop.
          join_unreachable("socket closed while joining");
          return;
        }
        // Later screens no longer need the relay; ignore.
        break;
    }
  }

  // Server never answered at all: don't strand the player.
  if ((screen_ == RoomHost || screen_ == RoomJoining) && room_code_.empty() &&
      rejoin_retry_ms_ <= 0) {
    signal_wait_ms_ += delta;
    if (signal_wait_ms_ > SIGNAL_TIMEOUT_MS) {
      if (rejoin_mode_) schedule_rejoin_retry("signal timeout", 1000);
      else if (hosting_) fall_back_to_manual("signal server timeout");
      else join_unreachable("signal server timeout");
    }
  }
}

void NetLobby::tick(int delta) {
  currentTime += delta;
  viewpoint += Point(1, 0) * (0.025 * delta);
  if (viewpoint.x() > WORLD_W) viewpoint += Point(-WORLD_W, 0);
  if (status_ms_ > 0) status_ms_ -= delta;

  pump_signal(delta);

  // Deck: bring the picker (and LAN rows) back the MOMENT the floating
  // keyboard is dismissed. The old proof — the next controller event
  // reaching us — left the keyboard-up layout on screen until the
  // player pressed something else (Glenn, Deck beta test). A dismiss we
  // issued ourselves (backing out, LAN join, and above all the
  // dismiss-then-reshow of a rate-limit bounce) latches the same
  // callback, so credit those to floating_kb_dismiss_pending_ first;
  // only an UNcounted dismiss is the user closing the keyboard, which
  // brings the picker/hints back. Without this the stale callback from a
  // same-frame dismiss+reshow hid the keyboard's layer and drew the hint
  // under the still-visible keyboard (Glenn: "TOO MANY RETRIES").
  if (steam_floating_keyboard_dismissed()) {
    if (floating_kb_dismiss_pending_ > 0) floating_kb_dismiss_pending_--;
    else if (floating_kb_up_) floating_kb_up_ = false;
  }

  // The LAN door (no-ops where NetLan isn't available or nothing runs).
  if (hosting_) lan_host_update(delta);
  else lan_join_update(delta);

  // Trickle ICE (M3-2b): relay candidates our transport gathers to the
  // peer as they appear ("mid\ncand" strings from the backend). Only after
  // our SDP has gone out: a candidate sent BEFORE its offer is buffered by
  // the relay and then wiped when the offer arrives (fresh-offer rule) —
  // and candidates can be gathered before the description is ready.
  if (signal_ && (hosting_ ? offer_sent_ : answer_sent_)) {
    NetTransport *t =
        transport_ ? transport_ : (session_ ? session_->transport() : nullptr);
    std::string c;
    while (t && t->poll_local_candidate(c)) {
      size_t nl = c.find('\n');
      if (nl != std::string::npos) {
        NET_LOG("[lobby] cand out: %s\n", c.substr(nl + 1, 40).c_str());
        signal_->send_cand(c.substr(0, nl), c.substr(nl + 1));
      }
    }
  }

  // A joined room whose host never offers (host quit for good — its close
  // frame was lost, or an old relay build without close support): fail
  // out instead of sitting on "JOINING THE ROOM" forever. Rejoin mode has
  // its own budget; a hostless-grace wait there is legitimate.
  if (!hosting_ && screen_ == RoomJoining && !rejoin_mode_) {
    join_wait_ms_ += delta;
    // Probing a maybe-our-own room (see the clipboard auto-join): a live
    // host answers in seconds, so a short window is enough — the long
    // one would be the old 45 s zombie-room hang.
    int limit = own_room_probe_ ? 8000 : 45000;
    if (join_wait_ms_ > limit) {
      join_wait_ms_ = 0;
      fail_headline_ = own_room_probe_ ? "NO ONE IS HOSTING THAT ROOM"
                                       : "THE HOST IS NOT RESPONDING";
      if (own_room_probe_) {
        set_status("THAT LOOKS LIKE YOUR OWN OLD ROOM");
        // The probed room never answered — remember it dead so the
        // clipboard auto-join can't walk back into the same 8 s probe on
        // the next CodeEntry visit (the iOS wedge: the phone's own old
        // link stays on the clipboard indefinitely).
        mark_room_dead(code_entry_);
      }
      own_room_probe_ = false;
      screen_ = LobbyFailed;
    }
  } else {
    join_wait_ms_ = 0;
    own_room_probe_ = false;
  }

  // Auto-rejoin retry loop (see schedule_rejoin_retry). The budget ticks
  // the WHOLE time we're hostless — not just between retries — so the
  // countdown the joiner sees is honest and the total wait is bounded
  // even while an attempt is in flight.
  if (rejoin_mode_ && !session_ && screen_ != LobbyFailed) {
    rejoin_budget_ms_ -= delta;
    if (rejoin_budget_ms_ <= 0) {
      rejoin_retry_ms_ = 0;
      fail_headline_ = "THE HOST DID NOT COME BACK";
      set_status("COULD NOT RECONNECT");
      screen_ = LobbyFailed;
      NET_LOG("[lobby] rejoin gave up (budget expired)\n");
    } else if (rejoin_retry_ms_ > 0) {
      // A retry is scheduled (schedule_rejoin_retry); fire it when the
      // countdown crosses zero — never while an attempt is in flight.
      rejoin_retry_ms_ -= delta;
      if (rejoin_retry_ms_ <= 0) {
        rejoin_retry_ms_ = 0;
        // A fresh virgin transport: an earlier attempt may have consumed an
        // offer, and the relay sends fresh TURN creds with each join.
        if (transport_) {
          transport_->close();
          delete transport_;
        }
        transport_ = NetTransport::create();
        if (transport_) {
          transport_->set_trickle(true);
          transport_->set_force_relay(s_join_force_relay);
        }
        ice_servers_.clear();
        answer_sent_ = false;
        signal_wait_ms_ = 0;
        NET_LOG("[lobby] rejoin retry: joining room %s\n", code_entry_.c_str());
        signal_->connect_join(net_signal_url(), code_entry_);
      }
    }
  }

  // JOIN convenience: a valid 5-letter code on the clipboard (the host
  // auto-copies theirs) is entered and joined without typing. Anything
  // else on the clipboard is silently ignored.
  if (code_clip_pending_ && screen_ != CodeEntry) code_clip_pending_ = false;
  if (code_clip_pending_) {
    std::string clip;
    if (net_clipboard_read_poll(clip)) {
      // Android 10+ only serves the clipboard to the FOCUSED window; right
      // after entering the screen the read can come back empty. Retry
      // briefly on touch platforms before concluding it is really empty.
      if (clip.empty() && is_touch_mode() && code_clip_retry_ms_ < 2000) {
        code_clip_retry_ms_ += delta;
        net_clipboard_read_start();  // poll again next tick
      } else {
        code_clip_pending_ = false;
        code_clip_retry_ms_ = 0;
        std::string code = room_code_from_clip(clip);
        bool ok = code.size() == (size_t)NET_ROOM_CODE_LEN;
        for (size_t i = 0; ok && i < code.size(); i++)
          ok = net_room_code_char_ok(code[i]);
        // Not a room code — maybe a manual INVITE blob (what the host's
        // no-server fallback copies)? Enter the manual flow right here:
        // the room relay is not involved, so drop it and stop trickling
        // (the blob and our reply each carry their candidates whole).
        // Taken for the auto-read too, not just the explicit paste
        // (desktop has no explicit paste on this screen: V is a code
        // letter, only controller X pastes) — decode_signal matches
        // nothing but Newtonia's own blob format, so arbitrary clipboard
        // content can't land here. This is the deliberate replacement
        // for the old silent timeout-into-manual fallback (see
        // join_unreachable).
        // ...but the LAN rows outrank the AUTOMATIC blob pickup. On a
        // shared clipboard (one box, or macOS Universal Clipboard between
        // one person's devices) the host's manual fallback copies its
        // INVITE blob right as the joiner's CodeEntry opens, and acting on
        // it here would steal the screen into the manual flow before the
        // beaconing host's row can even appear (field-hit on a one-box
        // mac test). Hold the auto pickup while LAN hosts are listed — or
        // while browse hasn't had time to hear a first beacon yet — and
        // let the 800 ms repoll re-offer the blob; if no host ever shows,
        // the manual flow proceeds as before. An explicit paste
        // (controller X) is user intent and is never held.
        std::string sdp;
        bool lan_hold =
            !code_clip_explicit_ && lan_browse_.running() &&
            (!lan_browse_.hosts().empty() || lan_browse_ms_ < 2500);
        bool is_blob = !ok && code_entry_.empty() && transport_ &&
                       Net::decode_signal(clip, sdp) == 'O';
        if (is_blob && lan_hold && !lan_blob_held_) {
          lan_blob_held_ = true;
          NET_LOG("[lobby] invite blob on clipboard held - lan rows first\n");
        }
        if (is_blob && !lan_hold) {
          NET_LOG("[lobby] manual invite found at code entry\n");
          code_clip_explicit_ = false;
          code_entry_keyboard(false);
          floating_kb_up_ = false;
          if (signal_) {
            signal_->close();
            delete signal_;
            signal_ = nullptr;
          }
          transport_->set_trickle(false);
          transport_->start_join(Net::strip_ice_candidates(sdp));
          screen_ = JoinGathering;
          return;
        }
        if (code_clip_explicit_ && !ok)
          set_status("NO ROOM CODE ON THE CLIPBOARD");
        // Never auto-join a room THIS process hosted (it closed the room
        // when it left) or one confirmed dead. An EXPLICIT paste (the
        // controller X hint) is user intent, like typing — no guards.
        if (ok && !code_clip_explicit_ &&
            (code == s_last_hosted_code || room_is_dead(code)))
          ok = false;
        // An own-room-SUSPECT auto-join (matches the persisted
        // last-hosted pref; probed below) walks the screen into an 8 s
        // RoomJoining probe — no LAN rows, no typing while it runs.
        // While LAN hosts are listed (or browse is still warming) hold
        // it exactly like the blob hold above: the row tap is the better
        // outcome, and on desktop the 800 ms repoll re-offers the code
        // if no host ever shows (the touch single read just skips this
        // visit). Field-hit on iOS: the phone's OWN old link, still on
        // the clipboard from an earlier hosting session, wedged
        // CodeEntry in a probe/fail loop while the Android host's
        // beacon had no row to land on.
        if (ok && !code_clip_explicit_ && code == g_prefs.last_hosted_code &&
            lan_hold) {
          NET_LOG("[lobby] own-room code on clipboard held - lan rows first\n");
          ok = false;
        }
        // A code matching only the PERSISTED last-hosted pref is
        // ambiguous: another live instance on this machine hosting right
        // now (the prefs INI is shared — mac host + mac client on one
        // box), or our own kill-orphaned room idling in its reclaim
        // grace. Probe it: join, but give up fast if no host offers —
        // a live room answers in seconds, the orphan never does.
        own_room_probe_ =
            ok && !code_clip_explicit_ && code == g_prefs.last_hosted_code;
        bool was_explicit = code_clip_explicit_;
        code_clip_explicit_ = false;
        if (ok && code_entry_.empty()) {
          code_entry_ = code;
          confirm();
          // AFTER confirm (which defaults the flag to player-driven):
          // an unsolicited clipboard probe fails silently.
          last_join_was_auto_ = !was_explicit;
        }
      }
    }
  }

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
  // Keep watching the clipboard while the join screen is idle (desktop
  // only: web reads need a user gesture, Android toasts every read).
  // The one-shot read above fires when the screen OPENS, which misses
  // anything copied after — the offline re-pair flow (Glenn): the game
  // ends, the joiner opens JOIN first, and only THEN does the host
  // re-host manually and auto-copy its fresh invite. This picks up a
  // late room code or invite blob within a second.
  if (screen_ == CodeEntry && !code_clip_pending_ && !is_touch_mode() &&
      code_entry_.empty()) {
    code_clip_repoll_ms_ += delta;
    if (code_clip_repoll_ms_ >= 800) {
      code_clip_repoll_ms_ = 0;
      code_clip_pending_ = true;
      net_clipboard_read_start();
    }
  } else if (screen_ != CodeEntry) {
    code_clip_repoll_ms_ = 0;
  }
#endif

  // Async clipboard read completion (web; immediate on native).
  if (paste_pending_ && net_clipboard_read_poll(paste_buffer_)) {
    paste_pending_ = false;
    if (paste_buffer_.empty())
      set_status("CLIPBOARD EMPTY OR BLOCKED");
    else
      handle_paste(paste_buffer_);
    paste_buffer_.clear();
  }

  if (transport_ && transport_->failed() && screen_ != LobbyFailed) {
    NET_LOG("[lobby] transport failed during signaling\n");
    if (rejoin_retry_after_session_loss("transport failed during signaling"))
      return;
    set_status("CONNECTION FAILED");
    screen_ = LobbyFailed;
    return;
  }

  switch (screen_) {
    case HostGathering:
      if (transport_ && transport_->local_description_ready()) {
        copy_local_description();
        set_status("");  // the screen itself says it was copied
        status_ms_ = 0;
        screen_ = HostWaitAnswer;
      }
      break;

    case JoinGathering:
      if (transport_ && transport_->local_description_ready()) {
        if (lan_joining_) {
          // LAN pairing: the answer rides the TCP socket, not the
          // clipboard (lan_join_update keeps pumping the flush).
          lan_browse_.send_answer(
              Net::encode_signal(false, transport_->local_description()));
        } else {
          copy_local_description();
        }
        status_ms_ = 0;
        screen_ = WaitConnect;
        connect_wait_ms_ = 0;
        session_ = new NetSession(transport_, NetSession::ClientRole);
        transport_ = nullptr;
      }
      break;

    case WaitConnect: {
      connect_wait_ms_ += delta;
      session_->update(delta);
      NetSession::Phase p = session_->phase();
      if (p == NetSession::Ready) {
        if (session_->role() == NetSession::HostRole) {
          // Host starts the game immediately; the session (and transport)
          // move into the GLGame, which streams snapshots to the peer.
          // In the room flow the signal connection moves too, keeping the
          // room open all game so the peer can rejoin (M2-4).
          NetSession *session = session_;
          session_ = nullptr;
          GLGame *game = new GLGame(session, (SDL_GameController *)0);
          game->net_set_worker_session(used_worker_);
          game->net_apply_peer_attestation(attested_peer_);
          // The advertised name freezes here for the session: the loss
          // re-beacon must repeat what a LAN joiner tapped and remembered
          // (its net_lan_host_name_), not a fresh local_host_name() read
          // that may have drifted (iOS Game Center alias).
          game->net_lan_beacon_name_ = lan_beacon_name_;
          if (signal_) {
            game->net_adopt_signal(signal_, room_code_, ice_servers_,
                                   room_token_);
            signal_ = nullptr;
          }
          handed_off_to_game_ = true;  // credential lifetime moves to the game
          request_state_change(game);
          return;
        }
        screen_ = Connected;
      } else if (p == NetSession::Rejected) {
        if (session_->reject_reason() == NetSession::RejectVersionMismatch) {
          fail_headline_ = "VERSION MISMATCH";
          set_status("UPDATE BOTH GAMES", 2 * STATUS_SHOW_MS);
        } else if (session_->reject_reason() == NetSession::RejectNotAllowed) {
          // Platform policy refused the pairing — the session decided this
          // inside the handshake (host: MSG_REJECT before WELCOME; client:
          // locally on the host's identity). Terminal, so a rejoin loop
          // can't thrash against a refusal that will never change.
          fail_headline_ = "CANNOT PLAY WITH THAT PLAYER";
          set_status("BLOCKED BY PLATFORM POLICY", 2 * STATUS_SHOW_MS);
        } else {
          fail_headline_ = "HOST REFUSED THE CONNECTION";
        }
        screen_ = LobbyFailed;
      } else if (p == NetSession::Failed ||
                 (session_->role() == NetSession::HostRole &&
                  connect_wait_ms_ > CONNECT_TIMEOUT_MS)) {
        // Only the host runs the clock here: it pasted the reply, so the
        // connection either establishes within seconds or never will. The
        // JOINER's wait includes the humans ferrying the reply code to the
        // host — minutes, not seconds — so it waits until the transport
        // actually fails (or the player backs out).
        NET_LOG("[lobby] connect failed: session_phase=%d waited=%d ms transport_failed=%d\n",
               (int)p, connect_wait_ms_,
               session_->transport() ? (int)session_->transport()->failed() : -1);
        if (rejoin_retry_after_session_loss("transport failed mid-handshake"))
          break;
        fail_headline_ = "COULD NOT CONNECT";
        set_status("A FIREWALL MAY BE BLOCKING THE GAME", 2 * STATUS_SHOW_MS);
        screen_ = LobbyFailed;
      }
      break;
    }

    case Connected: {
      session_->update(delta);
      // The host is already in-game streaming snapshots. The first complete
      // one bootstraps the client game: the save-restore constructor
      // rebuilds the world, then the snapshot's NetExtras are applied.
      std::vector<unsigned char> msg;
      while (session_->transport() && session_->transport()->poll(msg)) {
        Net::Reader r(msg.empty() ? nullptr : &msg[0], msg.size());
        Net::Header h;
        if (!Net::read_header(r, h)) continue;
        if (h.msg_type != Net::MSG_SNAPSHOT_CHUNK) continue;
        if (!assembler_.add_chunk(r)) continue;

        Save::MemStream in(assembler_.payload());
        Save::GameState s;
        if (!Save::deserialize_game(in, s)) continue;
        if (!net_state_sane(s)) continue;  // wait for a sane snapshot

        NetSession *session = session_;
        session_ = nullptr;
        GLGame *game = new GLGame(s, session, (SDL_GameController *)0);
        game->net_room_code_ = room_code_;  // enables client auto-rejoin
        game->net_set_worker_session(used_worker_);
        game->net_apply_peer_attestation(attested_peer_);
        // A LAN-door join remembers the host's NAME the way a relay join
        // remembers the code — it is the rejoin identity (round 4).
        game->net_lan_host_name_ = lan_host_name_;
        game->net_apply_extras(in, s);
        handed_off_to_game_ = true;  // credential lifetime moves to the game
        request_state_change(game);
        return;
      }
      if (session_->phase() == NetSession::Failed ||
          (session_->transport() && session_->transport()->failed())) {
        if (rejoin_retry_after_session_loss("transport flapped before snapshot"))
          break;
        set_status("CONNECTION LOST");
        screen_ = LobbyFailed;
      }
      break;
    }

    default:
      break;
  }
}

void NetLobby::draw() {
  glClear(GL_COLOR_BUFFER_BIT);
  glViewport(0, 0, window.x(), window.y());

  // Same drifting perspective starfield as the menu.
  float proj[16];
  mat4_perspective(proj, 90.0f, window.x() / window.y(), 100.0f, 2000.0f);
  for (int wrap = -1; wrap <= 1; wrap++) {
    float vp[16];
    mat4_translate(vp, proj, -viewpoint.x() + wrap * WORLD_W, -viewpoint.y(),
                   0.0f);
    gles2_set_vp(vp);
    starfield->draw_front(viewpoint);
    starfield->draw_rear(viewpoint);
  }

  float hw = window.x() / Overlay::SAFE_AREA_SCALE;
  float hh = window.y() / Overlay::SAFE_AREA_SCALE;
  float ortho[16];
  mat4_ortho(ortho, -hw, hw, -hh, hh, -1.0f, 1.0f);
  gles2_set_vp(ortho);

  // Touch: hoist the header toward the top edge — the soft keyboard eats
  // the bottom half on the code-entry screen, so vertical space is scarce.
  Typer::draw_centered(0, is_touch_mode() ? 480 : 320, "ONLINE CO-OP", 40);

  const int sz = 18, line = 52;
  int y = 120;
  std::vector<std::string> lines;
  // Index into `lines` of the one row that carries the verified tick (the
  // HOSTED BY badge), or -1. The tick is drawn larger than the text, so it
  // can't ride the string — the draw loop needs to know which row it is.
  int verified_line = -1;
  bool blink = (currentTime / 700) % 2 == 0;

  switch (screen_) {
    case Choose: {
      if (is_touch_mode()) {
        // The tap targets are the screen halves (see touch_tap): spread
        // the labels apart and skip the "> " cursor — a desktop cue.
        Typer::draw_centered(0, 180, "HOST", 30);
        Typer::draw_centered(0, -120, "JOIN", 30);
        y = -280;
        lines.push_back("HOST MAKES A ROOM CODE");
        lines.push_back("JOIN ENTERS A FRIEND'S CODE");
      } else {
        std::string host = std::string(selection_ == 0 ? "> " : "  ") + "HOST";
        std::string join = std::string(selection_ == 1 ? "> " : "  ") + "JOIN";
        Typer::draw_centered(0, 60, host.c_str(), 26);
        Typer::draw_centered(0, -40, join.c_str(), 26);
        lines.push_back("");
        y = -160;
        lines.push_back("HOST MAKES A ROOM CODE");
        lines.push_back("JOIN ENTERS A FRIEND'S CODE");
      }
      break;
    }
    case RoomHost:
      if (room_code_.empty()) {
        lines.push_back("CREATING A ROOM");
        if (blink) lines.push_back("PLEASE WAIT");
      } else if (is_touch_mode()) {
        // Spread over the full height under the hoisted header — the
        // default stack hugs the lower half of a phone screen.
        // Sparse on purpose (Glenn): the code + one clipboard/share line
        // say it all, and the LAN line only appears when a joiner is
        // actually mid-exchange.
        Typer::draw_centered(0, 340, "ROOM CODE", sz);
        Typer::draw_centered(0, 220, room_code_.c_str(), 48);
        if (net_share_available()) {
          kShareBand.draw("TAP TO SHARE");
        } else {
          Typer::draw_centered(0, -80, "COPIED TO CLIPBOARD", sz);
        }
        if (lan_announce_.running() && lan_announce_.peer_engaged())
          Typer::draw_centered(0, -180, "A LAN PLAYER IS CONNECTING", 12);
        if (blink)
          Typer::draw_centered(0, -220, "WAITING FOR PLAYER 2", sz);
      } else {
        lines.push_back("ROOM CODE");
        Typer::draw_centered(0, 20, room_code_.c_str(), 48);
        y = -100;
        lines.push_back("COPIED TO CLIPBOARD");
        lines.push_back("");
        // Pushed on the off-phase too (as a blank) — a conditional push
        // here makes every line below it jump each blink.
        lines.push_back(blink ? "WAITING FOR PLAYER 2" : "");
        if (lan_announce_.running() && lan_announce_.peer_engaged())
          lines.push_back("A LAN PLAYER IS CONNECTING");
      }
      break;
    case CodeEntry: {
      std::string slots;
      for (int i = 0; i < NET_ROOM_CODE_LEN; i++) {
        slots += (size_t)i < code_entry_.size() ? code_entry_[i] : '-';
        if (i + 1 < NET_ROOM_CODE_LEN) slots += ' ';
      }
      if (is_touch_mode()) {
        // The soft keyboard covers the lower half of the screen — stack
        // the heading, code and hint in the top half under the hoisted
        // header, with generous spacing. The RETURN TO MENU band at the
        // bottom is under the keyboard here, so the exit lives in the TOP
        // RIGHT instead (see touch_tap) — the centre of the top strip is
        // the ONLINE CO-OP header.
        kBackBand.draw("BACK");
        // LAN host bands under the code slots while hosts are visible
        // (tapped in touch_tap; a mismatched version still taps through
        // to lan_join_selected, which explains instead of joining). No
        // typing hint: the heading + blank slots say it all. With hosts
        // the code block compresses upward so three finger-sized bands
        // fit above the soft keyboard (see kLanBand).
        const std::vector<NetLan::HostInfo> &lh = lan_browse_.hosts();
        float lift = lan_band_lift();
        int fit = lan_bands_fit(lift);
        int show = (int)lh.size() > fit ? fit : (int)lh.size();
        if (show > 0) {
          if (lift > 0.0f) {
            // The measured keyboard reaches above the base band stack
            // (iPhone landscape ~60% coverage) — the heading and code
            // move up so the lifted bands have room underneath. Sit them
            // RELATIVE to the topmost visible band rather than at the
            // worst-case fixed spot: with one host row there is ~100
            // units of free air, and parking the code at the ceiling
            // crowded the ONLINE CO-OP header (field report). Clamped to
            // the old fixed position (345) so a tall keyboard + full
            // band list never pushes into the header.
            const TapBand &low = kLanBand[kLanBandCount - 1];
            float stack_top = (low.y - low.size) + (low.size + low.pad) +
                              lift + (show - 1) * kLanBandSpacing;
            float slots_y = stack_top + 76.0f;  // glyphs reach 2*28 down
            if (slots_y > 345.0f) slots_y = 345.0f;
            Typer::draw_centered(0, slots_y + 50.0f, "ENTER THE ROOM CODE",
                                 12);
            Typer::draw_centered(0, slots_y, slots.c_str(), 28);
          } else {
            // Clear of the ONLINE CO-OP title (glyphs reach ~y 400).
            Typer::draw_centered(0, 375, "ENTER THE ROOM CODE", 14);
            Typer::draw_centered(0, 315, slots.c_str(), 34);
          }
        } else {
          Typer::draw_centered(0, 360, "ENTER THE ROOM CODE", sz);
          Typer::draw_centered(0, 230, slots.c_str(), 48);
        }
        y = 80;
        for (int i = 0; i < show; i++) {
          std::string label =
              lh[i].proto == Net::PROTO_VERSION
                  ? "TAP TO JOIN " + lh[i].name
                  : lh[i].name + " - DIFFERENT VERSION";
          // Bottom-up fill: host 0 takes the LOWEST band.
          lan_band_at(kLanBandCount - show + i, lift).draw(label.c_str());
        }
      } else {
        // Heading + code live in the top half: the Steam Deck's floating
        // keyboard docks over the bottom half of the screen (same reason
        // the touch layout hoists them above the soft keyboard).
        Typer::draw_centered(0, 200, "ENTER THE ROOM CODE", sz);
        Typer::draw_centered(0, 120, slots.c_str(), 48);
        // The LAN rows live in the bottom half, which the Deck's floating
        // keyboard covers — mirror the touch layout's while-typing
        // visibility with a compact line in the free strip above the
        // header (keyboard input is consumed by the keyboard, so joining
        // has to wait for its dismissal anyway).
        if (floating_kb_up_ && !lan_browse_.hosts().empty()) {
          std::string top = "ON THIS NETWORK - " + lan_browse_.hosts()[0].name;
          if (lan_browse_.hosts().size() > 1) top += " +";
          Typer::draw_centered(0, 428, top.c_str(), 12);
          Typer::draw_centered(0, 392, "CLOSE THE KEYBOARD TO JOIN", 9);
        }
        // Controller flow: button hints replace the keyboard hint.
        // Hidden while the Deck's floating keyboard is up — the
        // keyboard IS the input then, and it types plain key events;
        // the first controller event that reaches us proves it was
        // dismissed (it consumes controller input while showing).
        // Drawn directly, not via the shared lines stack (its y sits
        // in the error text): -48 keeps breathing room under the
        // transient status (glyphs to ~-26) (Glenn, Deck, twice).
        // On hardware where the floating keyboard has actually shown
        // (Deck) the picker grid never draws at all — the keyboard is
        // the typing surface and Y re-summons it (Glenn), so the LAN
        // rows take the roomier keyboard-flow spots below instead.
        bool grid = controller_seen_ && !floating_kb_up_ &&
                    !floating_kb_available_;
        // The Deck hint omits B - DELETE: the floating keyboard owns
        // typing AND has its own backspace, so a delete key hint is
        // noise there (Glenn). B still backs out of a highlighted LAN
        // row and deletes a typed char — it's just not advertised.
        if (controller_seen_ && !floating_kb_up_)
          Typer::draw_centered(0, -48,
                               floating_kb_available_
                                   ? "X - PASTE   Y - KEYBOARD"
                                   : "A - TYPE   B - DELETE   X - PASTE",
                               sz);
        if (grid) {
          draw_picker();
          // LAN host rows under the picker grid (grid bottom ~ -204):
          // walking down off the grid's last row highlights them, A
          // joins, B backs out (see controller()).
          // Between the picker's second row (bottoms ~ -248) and the
          // RETURN TO MENU band text at -420.
          const std::vector<NetLan::HostInfo> &lh = lan_browse_.hosts();
          int show = lan_rows_shown();
          if (show > 0) {
            Typer::draw_centered(0, -262, "ON THIS NETWORK", 9);
            for (int i = 0; i < show; i++) {
              std::string row =
                  (lan_sel_ == i ? "> " : "  ") + lh[i].name;
              if (lh[i].proto != Net::PROTO_VERSION)
                row += " - DIFFERENT VERSION";
              Typer::draw_centered(0, -288.0f - (float)i * 32.0f,
                                   row.c_str(), lan_sel_ == i ? 14 : 11);
            }
            // The keyboard flow's join hint in the pad's vocabulary
            // (Glenn, Deck). The status line moved to the top half for
            // this layout, so the 2-row stack can reach down here
            // freely (bottom ~ -376, clear of the band text at -420).
            Typer::draw_centered(0, -296.0f - (float)show * 32.0f,
                                 "UP/DOWN AND A TO JOIN", 8);
          }
        } else {
          // LAN host rows clear of the header (y=320) and heading/code
          // (200/120) above; up/down moves the highlight (arrows or
          // pad — see keyboard()/picker_nav), Enter or A joins.
          const std::vector<NetLan::HostInfo> &lh = lan_browse_.hosts();
          int show = lan_rows_shown();
          if (show > 0) {
            Typer::draw_centered(0, -110, "ON THIS NETWORK", 12);
            for (int i = 0; i < show; i++) {
              std::string row =
                  (lan_sel_ == i ? "> " : "  ") + lh[i].name;
              if (lh[i].proto != Net::PROTO_VERSION)
                row += " - DIFFERENT VERSION";
              Typer::draw_centered(0, -160.0f - (float)i * 46.0f,
                                   row.c_str(), lan_sel_ == i ? 18 : 14);
            }
            // 14 extra under the last row: a SELECTED row's size-18
            // glyphs reach ~36 below their anchor, which left the hint
            // nearly touching the name (Glenn's screenshot).
            Typer::draw_centered(0, -174.0f - (float)show * 46.0f,
                                 controller_seen_
                                     ? "UP/DOWN AND A TO JOIN"
                                     : "UP/DOWN AND ENTER TO JOIN",
                                 10);
          }
        }
      }
      break;
    }
    case RoomJoining:
      if (rejoin_mode_) {
        // Honest about what's happening: the room survives the host's
        // socket for the reclaim grace window and we're waiting to see if
        // the host resumes — this is not a stuck join. The countdown is
        // the rejoin budget (ticks continuously; see the retry loop).
        lines.push_back("WAITING FOR THE HOST TO COME BACK");
        char left[40];
        snprintf(left, sizeof(left), "GIVING UP IN %d",
                 rejoin_budget_ms_ > 0 ? rejoin_budget_ms_ / 1000 + 1 : 0);
        lines.push_back(left);
        // LAN rejoin: the browse is live the whole wait, so show what it
        // hears. The remembered name still auto-joins the instant it
        // reappears; the rows are the manual escape hatch for a host
        // that comes back under a DIFFERENT name (re-signed into Game
        // Center, or the beacon renamed in the tap-vs-rename window) —
        // or for joining someone else instead of riding out the budget.
        // Same layout and inputs as the CodeEntry rows (tap bands on
        // touch, highlight + Enter/A elsewhere — see lan_rejoin_browsing
        // in confirm/nav_input/touch_tap).
        if (lan_rejoin_ && !lan_joining_) {
          const std::vector<NetLan::HostInfo> &lh = lan_browse_.hosts();
          if (is_touch_mode()) {
            float lift = lan_band_lift();
            int fit = lan_bands_fit(lift);
            int show = (int)lh.size() > fit ? fit : (int)lh.size();
            if (show > 0) y = -120;  // the bands own the y 49..225 strip
            for (int i = 0; i < show; i++) {
              std::string label =
                  lh[i].proto == Net::PROTO_VERSION
                      ? "TAP TO JOIN " + lh[i].name
                      : lh[i].name + " - DIFFERENT VERSION";
              lan_band_at(kLanBandCount - show + i, lift).draw(label.c_str());
            }
          } else {
            int show = lan_rows_shown();
            if (show > 0) {
              Typer::draw_centered(0, -110, "ON THIS NETWORK", 12);
              for (int i = 0; i < show; i++) {
                std::string row = (lan_sel_ == i ? "> " : "  ") + lh[i].name;
                if (lh[i].proto != Net::PROTO_VERSION)
                  row += " - DIFFERENT VERSION";
                Typer::draw_centered(0, -160.0f - (float)i * 46.0f,
                                     row.c_str(), lan_sel_ == i ? 18 : 14);
              }
              Typer::draw_centered(0, -174.0f - (float)show * 46.0f,
                                   controller_seen_
                                       ? "UP/DOWN AND A TO JOIN"
                                       : "UP/DOWN AND ENTER TO JOIN",
                                   10);
            }
          }
        }
      } else if (lan_joining_) {
        lines.push_back(("JOINING " + lan_host_name_).c_str());
        lines.push_back("ON THIS NETWORK");
        if (blink) lines.push_back("PLEASE WAIT");
      } else {
        lines.push_back("JOINING THE ROOM");
        if (blink) lines.push_back("PLEASE WAIT");
      }
      break;
    case HostGathering:
      lines.push_back("PREPARING YOUR INVITE CODE");
      if (blink) lines.push_back("PLEASE WAIT");
      break;
    case HostWaitAnswer:
      lines.push_back("INVITE CODE COPIED");
      lines.push_back("SEND IT TO YOUR FRIEND");
      lines.push_back("V - PASTE THEIR REPLY");
      lines.push_back("");
      lines.push_back("C - COPY THE INVITE AGAIN");
      if (lan_announce_.running() && lan_announce_.peer_engaged())
        lines.push_back("A LAN PLAYER IS CONNECTING");
      break;
    case JoinWaitOffer:
      lines.push_back("GET THE INVITE FROM YOUR HOST");
      lines.push_back("");
      lines.push_back("V - PASTE THE INVITE CODE");
      break;
    case JoinGathering:
      lines.push_back("PREPARING YOUR REPLY CODE");
      if (blink) lines.push_back("PLEASE WAIT");
      break;
    case WaitConnect:
      if (hosting_) {
        char waited[48];
        snprintf(waited, sizeof(waited), "CONNECTING %d", connect_wait_ms_ / 1000);
        lines.push_back(waited);
      } else if (signal_) {
        // Room flow: everything is automatic from here.
        lines.push_back("FOUND THE HOST");
        if (blink) lines.push_back("CONNECTING");
      } else {
        lines.push_back("REPLY CODE COPIED TO CLIPBOARD");
        lines.push_back("SEND IT BACK TO THE HOST");
        lines.push_back("");
        // Blank on the off-phase so the COPY hint below doesn't jump.
        lines.push_back(blink ? "WAITING FOR CONNECTION" : "");
        lines.push_back("");
        lines.push_back("C - COPY THE REPLY AGAIN");
      }
      break;
    case Connected: {
      // Only the joiner sees this screen, and only for the moment between
      // the handshake and the first world snapshot arriving.
      lines.push_back("CONNECTED!");
      lines.push_back("YOU ARE PLAYER 2");
      // The host's identity badge from the WELCOME append ("HOSTED BY
      // GLENN - STEAM"; a nameless host is player 1, "HOSTED BY
      // PLAYER 1 - DESKTOP"); a legacy host draws exactly the old screen.
      std::string badge;
      bool badge_verified = false;
      if (session_) {
        NetIdentity host_id = session_->peer_identity();  // claimed (WELCOME)
        net_apply_attested(host_id, attested_peer_);       // worker overlay
        NetIdentityCtx ctx = used_worker_ ? NET_ID_ONLINE : NET_ID_OFFLINE;
        badge = net_identity_badge_or(host_id, "PLAYER 1", ctx);
        badge_verified = net_identity_verified(host_id, ctx);
      }
      if (!badge.empty()) {
        if (badge_verified) verified_line = (int)lines.size();
        lines.push_back("HOSTED BY " + badge);
      }
      lines.push_back("");
      if (blink) lines.push_back("WAITING FOR THE HOST'S WORLD");
      break;
    }
    case LobbyFailed:
      lines.push_back(fail_headline_);
      lines.push_back("");
      lines.push_back(is_touch_mode() ? "TAP TO TRY AGAIN" : "ENTER - TRY AGAIN");
      break;
  }

  for (size_t i = 0; i < lines.size(); i++) {
    if (!lines[i].empty())
      Typer::draw_centered_verified(0, y - (int)i * line, lines[i].c_str(), sz,
                                    (int)i == verified_line);
  }

  // (The joiner waiting screens used to add a CANCEL band above the
  // return band; both exits stacked read as clutter — RETURN TO MENU is
  // the one universal exit, and an empty join screen is two taps away.)

  // The rejoin wait's persistent heading + countdown already say
  // everything the transient status could ("RECONNECTING", "ROOM
  // FOUND..."); two WAITING lines just read as clutter. Errors go to
  // LobbyFailed, so nothing informative is lost.
  bool status_redundant = rejoin_mode_ && screen_ == RoomJoining;
  if (status_ms_ > 0 && !status_.empty() && !status_redundant) {
    // Touch code entry: the usual status spot is behind the soft
    // keyboard; tuck it under the hint line instead.
    // CodeEntry hoists the status out of the bottom half on touch (soft
    // keyboard) AND in the controller layout (picker + LAN rows + join
    // hint own the space down to ~-376); it sits in the gap between the
    // code slots (glyphs to 24) and the button-hint line at -48 — low in
    // that gap, since at size 15 it descends to ~-26 and y 20 grazed the
    // code glyphs (Glenn, Deck).
    int sy = (screen_ == CodeEntry && (is_touch_mode() || controller_seen_))
                 ? 4
                 : -320;
    Typer::draw_centered(0, sy, status_.c_str(), 15);
  }

  // Touch: the bottom strip is a tap zone (see touch_tap), so label it as
  // an action rather than a key hint.
  TapBand::return_to_menu.draw(
      is_touch_mode() ? "RETURN TO MENU" : "ESC - BACK TO MENU", currentTime);
}

void NetLobby::keyboard(unsigned char key, int x, int y) {
  (void)x;
  (void)y;
  // Code entry happens on key-down: this is where real characters arrive
  // on every platform (GLUT char events, SDL_TEXTINPUT on mobile). The
  // key-up path must NOT also feed the code field — Android's touch layer
  // synthesizes key-ups ('p' from the pause zone, '\r' from taps) that
  // would otherwise leak stray characters into it.
  if (screen_ != CodeEntry) return;
  // Touch synthesizes '\r' on finger-down too, and a full code auto-joins,
  // so Enter is meaningless there — it would only flash the length hint.
  if (is_touch_mode() && (key == '\r' || key == '\n')) return;
  // Deck: a Steam-shortcut keyboard summon is invisible (Steamworks has
  // no SHOWN callback — Y is the observable path), but its keystrokes
  // are not: a typed character arriving while the controller layout is
  // up, on hardware where the floating keyboard has shown, means an OSD
  // is open over it. Flip on the first keystroke. Never fires on
  // desktop — floating_kb_available_ only latches where the keyboard
  // really shows.
  if (controller_seen_ && floating_kb_available_ && !floating_kb_up_ &&
      !is_touch_mode()) {
    bool typed = (key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z') ||
                 (key >= '0' && key <= '9') || key == 8 || key == 127;
    if (typed) floating_kb_up_ = true;
  }
  // Arrow keys (128+GLUT specials — code entry ignores them, so the
  // keys were free; deliberately NOT nav_key-translated, which would
  // turn them into W/A/D code letters).
  if (!is_touch_mode() && controller_seen_ && !floating_kb_up_) {
    // The picker is showing: arrows drive the same grid+LAN-row
    // navigation the pad does, and Enter acts like the pad's A —
    // type the highlighted character, or join a highlighted host.
    // Direct typing/backspace still work through code_entry_key.
    switch (key) {
      case 128 + 100: picker_nav('a'); return;  // left
      case 128 + 101: picker_nav('w'); return;  // up
      case 128 + 102: picker_nav('d'); return;  // right
      case 128 + 103: picker_nav('s'); return;  // down
    }
    if (key == '\r' || key == '\n') {
      controller_confirm();
      return;
    }
  } else if (!is_touch_mode() && lan_rows_shown() > 0) {
    // No picker (pure keyboard flow): up/down moves the highlight
    // between the discovered hosts and the code field (-1).
    int n = lan_rows_shown();
    if (key == 128 + 101) {  // up: code field wraps to the last row
      lan_sel_ = (lan_sel_ <= -1) ? n - 1 : lan_sel_ - 1;
      return;
    }
    if (key == 128 + 103) {  // down: past the last row = code field
      lan_sel_ = (lan_sel_ >= n - 1) ? -1 : lan_sel_ + 1;
      return;
    }
  }
  code_entry_key(key);
}

// Room-code typing: letters/digits from the code alphabet, backspace,
// Enter to submit. Runs INSTEAD of the shortcut keys — V, C and W are
// all valid code characters.
void NetLobby::code_entry_key(unsigned char key) {
  if (key == '\r' || key == '\n') {
    if (lan_sel_ >= 0) {  // a LAN host row is highlighted: join it
      lan_join_selected();
      return;
    }
    confirm();
    return;
  }
  if (key == 8 || key == 127) {  // backspace / delete
    if (!code_entry_.empty()) code_entry_.erase(code_entry_.size() - 1);
    return;
  }
  // Hidden TURN-test hook: codes never contain 0, so a leading zero arms
  // a relay-only join instead of feeding the code field. Two same-device
  // instances (the one-phone test, see TESTING.md) otherwise always pick
  // the direct ICE path and the relay goes unexercised.
  if (key == '0' && code_entry_.empty()) {
    s_join_force_relay = !s_join_force_relay;
    set_status(s_join_force_relay ? "RELAY-ONLY JOIN ARMED (TEST)"
                                  : "RELAY-ONLY JOIN DISARMED",
               8000);
    return;
  }
  bool alnum = (key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z') ||
               (key >= '0' && key <= '9');
  if (code_entry_.size() < (size_t)NET_ROOM_CODE_LEN &&
      net_room_code_char_ok((char)key)) {
    char c = (char)key;
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    code_entry_ += c;
    // Codes are fixed-length: join the moment the last slot fills.
    if (code_entry_.size() == (size_t)NET_ROOM_CODE_LEN) confirm();
  } else if (alnum && code_entry_.size() < (size_t)NET_ROOM_CODE_LEN) {
    // Deliberately absent from the code alphabet: 0/O, 1/I and 5/S are
    // confusable in the game font, F is the fullscreen key. Say so
    // instead of silently eating the keystroke.
    set_status("CODES NEVER USE 0 I OR F");
  }
}

void NetLobby::keyboard_up(unsigned char key, int x, int y) {
  (void)x;
  (void)y;
  key = nav_key(key);  // arrows navigate like WASD
  if (key == 27) {
    leave_to_menu();
    return;
  }
  // Touch platforms synthesize key-ups from finger zones ('p', '\r', ' ',
  // 'x'); touch_tap() owns all lobby interaction there, so acting on keys
  // here would double-handle taps (and corrupt the code field). Controller
  // input still works: it arrives via controller() -> nav_input directly.
  if (is_touch_mode()) return;
  // Code characters are consumed on key-down in keyboard(); swallowing
  // them here keeps V/C/W/S shortcuts from also firing while typing.
  if (screen_ == CodeEntry) return;
  nav_input(key);
}

// The single lobby decision ladder (see net_lobby.h) — everything that
// navigates off the CodeEntry screen lands here exactly once.
void NetLobby::nav_input(unsigned char key) {
  switch (key) {
    case 27:
      leave_to_menu();
      break;
    case 'w':
    case 'W':
      if (screen_ == Choose && selection_ > 0) selection_--;
      else if (lan_rejoin_browsing() && lan_sel_ >= 0)
        lan_sel_--;  // -1 = nothing highlighted
      break;
    case 's':
    case 'S':
      if (screen_ == Choose && selection_ < 1) selection_++;
      else if (lan_rejoin_browsing() && lan_sel_ < lan_rows_shown() - 1)
        lan_sel_++;
      break;
    case ' ':
    case '\r':
    case '\n':
      confirm();
      break;
    case 'v':
    case 'V':
      start_paste();
      break;
    case 'c':
    case 'C':
      copy_local_description();
      break;
    default:
      break;
  }
}

// One navigation model for the picker grid + the LAN host rows under
// it, spoken in logical wasd. Fed by BOTH the controller translator
// (dpad/stick) and the keyboard arrow keys — the picker shows whenever
// a controller has been seen, but a player with both devices in reach
// uses either, and the arrows previously did nothing there (Glenn).
void NetLobby::picker_nav(unsigned char key) {
  // Deck (floating keyboard proven): there is no picker grid — the
  // keyboard types, so up/down just walk the code field (-1) and the
  // LAN host rows, and left/right mean nothing (Glenn).
  if (floating_kb_available_) {
    if (key == 's' && lan_sel_ < lan_rows_shown() - 1) lan_sel_++;
    else if (key == 'w' && lan_sel_ >= 0) lan_sel_--;
    return;
  }
  switch (key) {
    case 'w':
      if (lan_sel_ >= 0) lan_sel_--;  // -1 = back onto the grid
      else picker_move(0, -1);
      break;
    case 's':
      if (lan_sel_ >= 0) {
        if (lan_sel_ < lan_rows_shown() - 1) lan_sel_++;
      } else if (lan_rows_shown() > 0 && picker_on_bottom_row()) {
        lan_sel_ = 0;  // walk off the grid into the rows
      } else {
        picker_move(0, 1);
      }
      break;
    case 'a':
      if (lan_sel_ < 0) picker_move(-1, 0);
      break;
    case 'd':
      if (lan_sel_ < 0) picker_move(1, 0);
      break;
    default:
      break;
  }
}

// Wraps the picker selection through the alphabet: dx walks the strip
// (wrapping row to row), dy jumps a row keeping the column.
void NetLobby::picker_move(int dx, int dy) {
  int n = (int)strlen(NET_ROOM_CODE_ALPHABET);
  if (dx) picker_index_ = (picker_index_ + dx + n) % n;
  if (dy) {
    int rows = (n + PICKER_COLS - 1) / PICKER_COLS;
    int row = (picker_index_ / PICKER_COLS + dy + rows) % rows;
    picker_index_ = row * PICKER_COLS + picker_index_ % PICKER_COLS;
    if (picker_index_ >= n) picker_index_ = n - 1;  // short last row
  }
}

// A / right trigger: on CodeEntry it types the picker's character (the
// join fires automatically when the last slot fills, same as typing);
// everywhere else it is ENTER.
void NetLobby::controller_confirm() {
  // Touch platforms never draw the picker (the soft keyboard owns code
  // entry there), so a paired controller must not type from it blind.
  if (screen_ == CodeEntry && !is_touch_mode()) {
    if (lan_sel_ >= 0) {  // a LAN host row is highlighted: join it
      lan_join_selected();
      return;
    }
    // Deck: no picker to type from — A acts like Enter on the typed
    // code (join on full, length hint otherwise).
    if (floating_kb_available_) {
      confirm();
      return;
    }
    code_entry_key((unsigned char)NET_ROOM_CODE_ALPHABET[picker_index_]);
    return;
  }
  confirm();
}

// The picker's last (possibly short) row — walking down off it enters
// the LAN host rows drawn underneath.
bool NetLobby::picker_on_bottom_row() const {
  int n = (int)strlen(NET_ROOM_CODE_ALPHABET);
  int rows = (n + PICKER_COLS - 1) / PICKER_COLS;
  return picker_index_ / PICKER_COLS == rows - 1;
}

// How many LAN host rows the current screen draws — the picker layout
// fits 2 under the grid; the keyboard layout AND the Deck's gridless
// controller layout (floating keyboard proven, rows in the same
// roomier spots) fit 3. Draw and selection both use this so the
// highlight can never land on an invisible host.
int NetLobby::lan_rows_shown() const {
  int n = (int)lan_browse_.hosts().size();
  int cap = (controller_seen_ && !floating_kb_available_) ? 2 : 3;
  return n > cap ? cap : n;
}

void NetLobby::controller(SDL_Event event) {
  // Only real controller events prove a pad: platform loops (web_main's
  // default case in particular) forward OTHER unhandled event types
  // through here too, and those must not summon the picker.
  if (event.type == SDL_CONTROLLERBUTTONDOWN ||
      event.type == SDL_CONTROLLERAXISMOTION) {
    controller_seen_ = true;
    // The Deck's floating keyboard consumes BUTTON input while it is
    // showing, so a button press reaching us proves it was dismissed and
    // the picker may come back. AXIS events do NOT count: stick drift
    // and gyro noise stream through the keyboard continuously, and
    // clearing on them brought the picker back underneath it within
    // milliseconds ("both keyboards are showing"). A deliberate hard
    // flick past the nav threshold also counts as proof.
    if (event.type == SDL_CONTROLLERBUTTONDOWN ||
        (event.type == SDL_CONTROLLERAXISMOTION &&
         (event.caxis.value > NAV_STICK_ON || event.caxis.value < -NAV_STICK_ON)))
      floating_kb_up_ = false;
  }

  // X (paste) and Y (copy) are lobby shortcuts on every screen, not nav.
  if (event.type == SDL_CONTROLLERBUTTONDOWN) {
    if (event.cbutton.button == SDL_CONTROLLER_BUTTON_X) {
      // CodeEntry advertises X - PASTE: re-run the clipboard CODE read
      // as an explicit paste. start_paste() is the manual flow's SDP
      // blob path and ignores a bare room code.
      if (screen_ == CodeEntry) {
        code_entry_.clear();  // the pasted code replaces partial typing
        code_clip_explicit_ = true;
        code_clip_pending_ = true;
        net_clipboard_read_start();
      } else {
        start_paste();
      }
      return;
    }
    if (event.cbutton.button == SDL_CONTROLLER_BUTTON_Y) {
      // Deck: Y re-summons the floating keyboard on CodeEntry (its copy
      // meaning is useless there — nothing local to copy yet). Steam
      // has no keyboard-SHOWN callback, so a Steam+X summon leaves the
      // layout stuck in controller mode (Glenn); Y routes the summon
      // through the game so the mode toggles reliably both ways.
      if (screen_ == CodeEntry && floating_kb_available_ &&
          !is_touch_mode()) {
        floating_kb_up_ = code_entry_keyboard(true);
        return;
      }
      copy_local_description();
      return;
    }
  }

  // CodeEntry keeps its richer pad semantics, but the raw dpad/stick/
  // trigger decoding (and its arm/release hysteresis) is the shared
  // translator's job — the picker just remaps the logical keys: w/a/s/d
  // drive the character grid, Enter (A or the right trigger — the in-game
  // fire button, what a Steam Deck player reaches for) types the picker's
  // character, Esc (B/Back) deletes before it backs out. Sharing the
  // translator also shares its latches, so a stick held across the
  // Choose→CodeEntry transition stays armed instead of double-stepping.
  if (screen_ == CodeEntry) {
    if (event.type == SDL_CONTROLLERBUTTONDOWN &&
        event.cbutton.button == SDL_CONTROLLER_BUTTON_B &&
        !is_touch_mode() && lan_sel_ >= 0) {
      // B steps back out of the LAN host rows before it deletes/leaves.
      lan_sel_ = -1;
      return;
    }
    if (event.type == SDL_CONTROLLERBUTTONDOWN &&
        event.cbutton.button == SDL_CONTROLLER_BUTTON_B &&
        !is_touch_mode() && !code_entry_.empty()) {
      // Console convention: B deletes while there is something to delete,
      // and only backs out of an empty code field (non-touch only — the
      // soft keyboard owns code entry on touch).
      code_entry_key(8);
      return;
    }
    if (event.type == SDL_CONTROLLERBUTTONDOWN &&
        event.cbutton.button == SDL_CONTROLLER_BUTTON_START)
      return;  // Start is not a picker key (A/RT type, and joins on full)
    // LAN host rows sit BELOW the picker grid (max 2 drawn): walking down
    // off the grid's bottom row enters them, up from the top row returns
    // to the grid, A joins the highlighted host (controller_confirm), B
    // backs out (above). lan_sel_ -1 = the picker owns the pad.
    unsigned char nk = nav_key_from_controller(event);
    switch (nk) {
      case 'w': case 's': case 'a': case 'd':
        picker_nav(nk);
        break;
      case '\r': controller_confirm(); break;
      case 27:   leave_to_menu(); break;
      default:   break;
    }
    return;
  }

  // Every other lobby screen speaks the shared nav language: d-pad/stick =
  // w/s, A/Start/right-trigger = confirm, B/Back = Esc (leave to menu).
  unsigned char k = nav_key_from_controller(event);
  if (k) nav_input(k);
}

void NetLobby::touch_tap(float nx, float ny) {
  // Bottom strip on every screen = RETURN TO MENU.
  if (TapBand::return_to_menu.contains(nx, ny)) {
    leave_to_menu();
    return;
  }
  switch (screen_) {
    case Choose:
      // Upper band = HOST, lower band = JOIN (mirrors the drawn rows).
      selection_ = (ny < 0.5f) ? 0 : 1;
      confirm();
      break;
    case CodeEntry:
      // Top-right = BACK: the usual bottom exit band is physically under
      // the soft keyboard, so this is the only reachable way out of code
      // entry on touch.
      if (kBackBand.contains(nx, ny)) {
        leave_to_menu();
        return;
      }
      // LAN host bands (drawn in place of the typing hint). A version
      // mismatch taps through to the explanatory status message.
      {
        const std::vector<NetLan::HostInfo> &lh = lan_browse_.hosts();
        float lift = lan_band_lift();
        int fit = lan_bands_fit(lift);
        int show = (int)lh.size() > fit ? fit : (int)lh.size();
        for (int i = 0; i < show; i++) {
          // Mirror of the draw's bottom-up fill (and its keyboard lift).
          if (lan_band_at(kLanBandCount - show + i, lift).contains(nx, ny)) {
            lan_sel_ = i;
            lan_join_selected();
            return;
          }
        }
      }
      // Re-summon a dismissed soft keyboard (touch; no-op elsewhere).
      floating_kb_up_ = code_entry_keyboard(true);
        if (floating_kb_up_) floating_kb_available_ = true;
      break;
    case RoomHost:
      if (kShareBand.contains(nx, ny) && !room_code_.empty() &&
          net_share_available())
        // Share ONE universal link, regardless of the host's platform: the
        // recipient's device resolves it at tap time (invites.h). Installed
        // iOS/Android apps auto-open via Universal/App Links; Steam desktop
        // gets a "Join on Steam" button; everyone else falls through to the
        // browser game, which reads ?code= and joins. The bare code still
        // rides along in the text for manual entry.
        net_share_text("Join my Newtonia co-op game: " + net_join_url(room_code_) +
                       "  (room code: " + room_code_ + ")");
      break;
    case RoomJoining:
      // The rejoin wait screen's live host rows (see draw): the manual
      // escape hatch when the host came back under a different name.
      if (lan_rejoin_browsing()) {
        const std::vector<NetLan::HostInfo> &lh = lan_browse_.hosts();
        float lift = lan_band_lift();
        int fit = lan_bands_fit(lift);
        int show = (int)lh.size() > fit ? fit : (int)lh.size();
        for (int i = 0; i < show; i++) {
          // Mirror of the draw's bottom-up fill.
          if (lan_band_at(kLanBandCount - show + i, lift).contains(nx, ny)) {
            lan_sel_ = i;
            lan_join_selected();
            return;
          }
        }
      }
      break;
    case LobbyFailed:
      confirm();  // same as ENTER: back to the choose screen
      break;
    default:
      break;  // waiting screens have nothing to tap
  }
}

bool NetLobby::back_pressed() {
  leave_to_menu();
  return true;
}
