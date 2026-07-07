#include "net_lobby.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "gl_compat.h"
#include "glgame.h"
#include "glstarfield.h"
#include "mat4.h"
#include "menu.h"
#include "net_session.h"
#include "net_signal.h"
#include "net_transport.h"
#include "preferences.h"
#include "steam_build.h"
#include "typer.h"
#include "view/overlay.h"
#include "view/tap_band.h"

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
static std::string s_dead_code;  // room confirmed dead (host BYE / relay says gone)

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
// RoomHost: the "TAP HERE TO SHARE IT" line, padded to finger height.
const TapBand kShareBand(0.5f, -80, 18, 42.0f);

// CodeEntry controller picker: the code alphabet as a grid under the
// code slots (desktop layout — touch uses the soft keyboard instead).
// Two rows sit between the button-hint line at -20 (glyphs reach ~-56)
// and the transient status line at -320 (selected cells grow to size 22
// → 44 tall, so the second row bottoms out around -226).
const int PICKER_COLS = 15;
const float PICKER_TOP_Y = -130.0f;
const float PICKER_ROW_H = 52.0f;
const float PICKER_CELL_W = 45.0f;

// Stick navigation hysteresis: a move arms at half deflection and only
// re-arms once the stick falls back under a quarter — a light nudge does
// nothing, and wobble around a single threshold can't re-trigger (a
// plain ±8000 edge felt twitchy on a real gamepad).
const int STICK_ON = 16000, STICK_OFF = 8000;

}  // namespace

void NetLobby::mark_room_dead(const std::string &code) { s_dead_code = code; }

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
}

NetLobby::NetLobby(const std::string &rejoin_code) : NetLobby() {
  hosting_ = false;
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
  set_status("RECONNECTING...");
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
static bool code_entry_keyboard(bool open) {
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
  steam_dismiss_floating_keyboard();
  return false;
}

// A rejoin attempt failed in a retryable way (network still down, relay
// briefly unreachable): stay on the joining screen and try again.
void NetLobby::schedule_rejoin_retry(const char *why, int delay_ms) {
  NET_LOG("[lobby] rejoin retry in %d ms (%s)\n", delay_ms, why);
  set_status("RECONNECTING...");
  screen_ = RoomJoining;
  signal_wait_ms_ = 0;
  if (rejoin_retry_ms_ <= 0) rejoin_retry_ms_ = delay_ms;
}

NetLobby::~NetLobby() {
  delete session_;  // closes + deletes the transport it owns
  if (!session_ && transport_) {
    transport_->close();
    delete transport_;
  }
  if (signal_) {
    signal_->close();
    delete signal_;
  }
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
  screen_ = Choose;
}

void NetLobby::leave_to_menu() {
  code_entry_keyboard(false);
  floating_kb_up_ = false;
  // Host abandoning the room: kill it at the relay now, or its code stays
  // claimable-but-hostless for the whole reclaim grace window.
  if (hosting_ && signal_) signal_->send_close();
  request_state_change(new Menu());
}

void NetLobby::confirm() {
  if (screen_ == Choose) {
    hosting_ = (selection_ == 0);
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
      if (signal_) {
        screen_ = CodeEntry;
        floating_kb_up_ = code_entry_keyboard(true);
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
      // Applied here, not at transport creation: the "0" arming happens
      // while typing, after the transport already exists. The pc (and its
      // ICE policy) is only built at start_join, so this is in time.
      if (transport_) transport_->set_force_relay(s_join_force_relay);
      signal_->connect_join(net_signal_url(), code_entry_);
      signal_wait_ms_ = 0;
      screen_ = RoomJoining;
    } else {
      set_status("THE CODE IS 5 LETTERS");
    }
  } else if (screen_ == LobbyFailed) {
    // TRY AGAIN after a failed JOIN goes straight back to an empty join
    // screen; only a failed HOST attempt returns to the chooser.
    if (!hosting_ || rejoin_mode_) {
      retry_join();
    } else {
      reset_to_choose();
    }
  }
}

// Abandon the current attempt and land on an EMPTY join screen with a
// fresh transport/signal. Used by TRY AGAIN and the joining-screen
// CANCEL band.
void NetLobby::retry_join() {
  reset_to_choose();
  selection_ = 1;
  confirm();  // -> CodeEntry
  // The code that just failed (or is being cancelled) is likely still on
  // the clipboard — don't auto-join it again.
  code_clip_pending_ = false;
  code_entry_.clear();
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
  set_status("NO ROOM SERVER - USING MANUAL CODES");
  // The clipboard blob must carry every candidate: back to non-trickle
  // (always before start_* — the host deferred its start to the Room
  // frame that never came, the joiner starts at paste time).
  if (transport_) transport_->set_trickle(false);
  if (hosting_ && transport_) transport_->start_host();  // was deferred
  screen_ = hosting_ ? HostGathering : JoinWaitOffer;
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
        s_last_hosted_code = room_code_;  // never auto-join our own room
        // Persist it too: a killed-and-relaunched app loses the static, and
        // its own code is still on the clipboard — the auto-join would walk
        // the ex-host straight into its own dead room.
        g_prefs.last_hosted_code = room_code_;
        save_preferences();
        net_clipboard_write(room_code_);  // ready to paste to the friend
        NET_LOG("[lobby] room %s (%d turn servers)\n", room_code_.c_str(),
               (int)ice_servers_.size());
        if (transport_) {
          transport_->set_ice_servers(ice_servers_);
          transport_->start_host();  // deferred from confirm()
        }
        break;
      case NetSignal::Event::Joined:
        room_code_ = code_entry_;  // acked — also disarms the timeout below
        set_status("ROOM FOUND - WAITING FOR THE HOST");
        break;
      case NetSignal::Event::PeerJoin:
        set_status("PLAYER 2 IS CONNECTING...");
        break;
      case NetSignal::Event::PeerLeave:
        set_status("PLAYER 2 LEFT THE ROOM");
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
          if (ev.text == "no-such-room") set_status("NO ROOM WITH THAT CODE");
          else if (ev.text == "room-full") set_status("THAT ROOM IS FULL");
          else if (ev.text == "rate-limited") set_status("TOO MANY TRIES - WAIT A MINUTE");
          else if (ev.text == "host-closed") set_status("THAT SERVER WAS SHUT DOWN");
          else set_status("THE ROOM HAS EXPIRED");
          // Any of these except rate-limited means the code is dead — stop
          // the clipboard auto-join from walking back into it.
          if (ev.text != "rate-limited") mark_room_dead(code_entry_);
          room_code_.clear();
          code_entry_.clear();
          answer_sent_ = false;
          screen_ = CodeEntry;
          floating_kb_up_ = code_entry_keyboard(true);
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
          fall_back_to_manual("socket closed while joining");
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
      else fall_back_to_manual("signal server timeout");
    }
  }
}

void NetLobby::tick(int delta) {
  currentTime += delta;
  viewpoint += Point(1, 0) * (0.025 * delta);
  if (viewpoint.x() > WORLD_W) viewpoint += Point(-WORLD_W, 0);
  if (status_ms_ > 0) status_ms_ -= delta;

  pump_signal(delta);

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
      if (own_room_probe_) set_status("THAT LOOKS LIKE YOUR OWN OLD ROOM");
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
        std::string code;
        for (size_t i = 0; i < clip.size(); i++) {
          char c = clip[i];
          if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
          if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
          code += c;
        }
        bool ok = code.size() == (size_t)NET_ROOM_CODE_LEN;
        for (size_t i = 0; ok && i < code.size(); i++)
          ok = net_room_code_char_ok(code[i]);
        if (code_clip_explicit_ && !ok)
          set_status("NO ROOM CODE ON THE CLIPBOARD");
        // Never auto-join a room THIS process hosted (it closed the room
        // when it left) or one confirmed dead. An EXPLICIT paste (the
        // controller X hint) is user intent, like typing — no guards.
        if (ok && !code_clip_explicit_ &&
            (code == s_last_hosted_code || code == s_dead_code))
          ok = false;
        // A code matching only the PERSISTED last-hosted pref is
        // ambiguous: another live instance on this machine hosting right
        // now (the prefs INI is shared — mac host + mac client on one
        // box), or our own kill-orphaned room idling in its reclaim
        // grace. Probe it: join, but give up fast if no host offers —
        // a live room answers in seconds, the orphan never does.
        own_room_probe_ =
            ok && !code_clip_explicit_ && code == g_prefs.last_hosted_code;
        code_clip_explicit_ = false;
        if (ok && code_entry_.empty()) {
          code_entry_ = code;
          confirm();
        }
      }
    }
  }

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
        copy_local_description();
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
          if (signal_) {
            game->net_adopt_signal(signal_, room_code_, ice_servers_,
                                   room_token_);
            signal_ = nullptr;
          }
          request_state_change(game);
          return;
        }
        screen_ = Connected;
      } else if (p == NetSession::Rejected) {
        if (session_->reject_reason() == NetSession::RejectVersionMismatch) {
          fail_headline_ = "VERSION MISMATCH";
          set_status("UPDATE BOTH GAMES", 2 * STATUS_SHOW_MS);
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
        game->net_apply_extras(in, s);
        request_state_change(game);
        return;
      }
      if (session_->phase() == NetSession::Failed ||
          (session_->transport() && session_->transport()->failed())) {
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
        if (blink) lines.push_back("PLEASE WAIT...");
      } else if (is_touch_mode()) {
        // Spread over the full height under the hoisted header — the
        // default stack hugs the lower half of a phone screen.
        Typer::draw_centered(0, 340, "ROOM CODE", sz);
        Typer::draw_centered(0, 220, room_code_.c_str(), 48);
        Typer::draw_centered(0, 40, "TELL YOUR FRIEND THE CODE", sz);
        if (net_share_available()) {
          kShareBand.draw("TAP HERE TO SHARE IT");
        } else {
          Typer::draw_centered(0, -80, "IT IS ON YOUR CLIPBOARD", sz);
        }
        if (blink)
          Typer::draw_centered(0, -220, "WAITING FOR PLAYER 2...", sz);
      } else {
        lines.push_back("ROOM CODE");
        Typer::draw_centered(0, 20, room_code_.c_str(), 48);
        y = -100;
        lines.push_back("TELL YOUR FRIEND THE CODE");
        lines.push_back("IT IS ON YOUR CLIPBOARD");
        lines.push_back("");
        if (blink) lines.push_back("WAITING FOR PLAYER 2...");
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
        Typer::draw_centered(0, 360, "ENTER THE ROOM CODE", sz);
        Typer::draw_centered(0, 230, slots.c_str(), 48);
        y = 80;
        lines.push_back("TYPE THE CODE YOUR HOST SEES");
      } else {
        // Heading + code live in the top half: the Steam Deck's floating
        // keyboard docks over the bottom half of the screen (same reason
        // the touch layout hoists them above the soft keyboard).
        Typer::draw_centered(0, 200, "ENTER THE ROOM CODE", sz);
        Typer::draw_centered(0, 120, slots.c_str(), 48);
        y = -20;
        if (controller_seen_ && !floating_kb_up_) {
          // Controller flow: button hints replace the keyboard hint,
          // picker grid below. Hidden while the Deck's floating
          // keyboard is up — the keyboard IS the input then, and it
          // types plain key events; the first controller event that
          // reaches us proves it was dismissed (it consumes controller
          // input while showing) and brings the picker back.
          lines.push_back("A - TYPE   B - DELETE   X - PASTE");
          draw_picker();
        } else {
          lines.push_back("TYPE THE CODE YOUR HOST SEES");
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
      } else {
        lines.push_back("JOINING THE ROOM");
        if (blink) lines.push_back("PLEASE WAIT...");
      }
      break;
    case HostGathering:
      lines.push_back("PREPARING YOUR INVITE CODE");
      if (blink) lines.push_back("PLEASE WAIT...");
      break;
    case HostWaitAnswer:
      lines.push_back("1. INVITE CODE COPIED - SEND IT TO YOUR FRIEND");
      lines.push_back("2. THEY JOIN AND SEND YOU A REPLY CODE");
      lines.push_back("3. COPY THE REPLY, THEN PRESS V HERE");
      lines.push_back("");
      lines.push_back("C - COPY THE INVITE AGAIN");
      break;
    case JoinWaitOffer:
      lines.push_back("GET THE INVITE CODE FROM YOUR HOST");
      lines.push_back("");
      lines.push_back("V - PASTE THE INVITE CODE");
      break;
    case JoinGathering:
      lines.push_back("PREPARING YOUR REPLY CODE");
      if (blink) lines.push_back("PLEASE WAIT...");
      break;
    case WaitConnect:
      if (hosting_) {
        char waited[48];
        snprintf(waited, sizeof(waited), "CONNECTING... %d", connect_wait_ms_ / 1000);
        lines.push_back(waited);
      } else if (signal_) {
        // Room flow: everything is automatic from here.
        lines.push_back("FOUND THE HOST");
        if (blink) lines.push_back("CONNECTING...");
      } else {
        lines.push_back("REPLY CODE COPIED TO CLIPBOARD");
        lines.push_back("SEND IT BACK TO THE HOST");
        lines.push_back("");
        if (blink) lines.push_back("WAITING FOR CONNECTION...");
        lines.push_back("");
        lines.push_back("C - COPY THE REPLY AGAIN");
      }
      break;
    case Connected:
      // Only the joiner sees this screen, and only for the moment between
      // the handshake and the first world snapshot arriving.
      lines.push_back("CONNECTED!");
      lines.push_back("YOU ARE PLAYER 2");
      lines.push_back("");
      if (blink) lines.push_back("WAITING FOR THE HOST'S WORLD...");
      break;
    case LobbyFailed:
      lines.push_back(fail_headline_);
      lines.push_back("");
      lines.push_back(is_touch_mode() ? "TAP TO TRY AGAIN" : "ENTER - TRY AGAIN");
      break;
  }

  for (size_t i = 0; i < lines.size(); i++) {
    if (!lines[i].empty())
      Typer::draw_centered(0, y - (int)i * line, lines[i].c_str(), sz);
  }

  // (The joiner waiting screens used to add a CANCEL band above the
  // return band; both exits stacked read as clutter — RETURN TO MENU is
  // the one universal exit, and an empty join screen is two taps away.)

  // The rejoin wait's persistent heading + countdown already say
  // everything the transient status could ("RECONNECTING...", "ROOM
  // FOUND..."); two WAITING lines just read as clutter. Errors go to
  // LobbyFailed, so nothing informative is lost.
  bool status_redundant = rejoin_mode_ && screen_ == RoomJoining;
  if (status_ms_ > 0 && !status_.empty() && !status_redundant) {
    // Touch code entry: the usual status spot is behind the soft
    // keyboard; tuck it under the hint line instead.
    int sy = (is_touch_mode() && screen_ == CodeEntry) ? 20 : -320;
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
  code_entry_key(key);
}

// Room-code typing: letters/digits from the code alphabet, backspace,
// Enter to submit. Runs INSTEAD of the shortcut keys — V, C and W are
// all valid code characters.
void NetLobby::code_entry_key(unsigned char key) {
  if (key == '\r' || key == '\n') {
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
    set_status("CODES NEVER USE 0 O 1 I 5 S OR F");
  }
}

void NetLobby::keyboard_up(unsigned char key, int x, int y) {
  (void)x;
  (void)y;
  if (key == 27) {
    leave_to_menu();
    return;
  }
  // Touch platforms synthesize key-ups from finger zones ('p', '\r', ' ',
  // 'x'); touch_tap() owns all lobby interaction there, so acting on keys
  // here would double-handle taps (and corrupt the code field).
  if (is_touch_mode()) return;
  // Code characters are consumed on key-down in keyboard(); swallowing
  // them here keeps V/C/W/S shortcuts from also firing while typing.
  if (screen_ == CodeEntry) return;
  switch (key) {
    case 'w':
    case 'W':
      if (screen_ == Choose && selection_ > 0) selection_--;
      break;
    case 's':
    case 'S':
      if (screen_ == Choose && selection_ < 1) selection_++;
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
    code_entry_key((unsigned char)NET_ROOM_CODE_ALPHABET[picker_index_]);
    return;
  }
  confirm();
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
         (event.caxis.value > STICK_ON || event.caxis.value < -STICK_ON)))
      floating_kb_up_ = false;
  }
  if (event.type == SDL_CONTROLLERBUTTONDOWN) {
    switch (event.cbutton.button) {
      case SDL_CONTROLLER_BUTTON_DPAD_UP:
        if (screen_ == CodeEntry) picker_move(0, -1);
        else if (screen_ == Choose && selection_ > 0) selection_--;
        break;
      case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        if (screen_ == CodeEntry) picker_move(0, 1);
        else if (screen_ == Choose && selection_ < 1) selection_++;
        break;
      case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        if (screen_ == CodeEntry) picker_move(-1, 0);
        break;
      case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        if (screen_ == CodeEntry) picker_move(1, 0);
        break;
      case SDL_CONTROLLER_BUTTON_A:
        controller_confirm();
        break;
      case SDL_CONTROLLER_BUTTON_X:
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
        break;
      case SDL_CONTROLLER_BUTTON_Y:
        copy_local_description();
        break;
      case SDL_CONTROLLER_BUTTON_B:
        // Console convention: B deletes while there is something to
        // delete, and only backs out of an empty code field (non-touch
        // only — the soft keyboard owns code entry on touch).
        if (screen_ == CodeEntry && !is_touch_mode() &&
            !code_entry_.empty()) {
          code_entry_key(8);
          break;
        }
        leave_to_menu();
        break;
      case SDL_CONTROLLER_BUTTON_BACK:
        leave_to_menu();
        break;
      default:
        break;
    }
    return;
  }
  if (event.type != SDL_CONTROLLERAXISMOTION) return;
  // Left stick mirrors the d-pad and the right trigger mirrors A (it is
  // the in-game fire button, so it is what a Steam Deck player reaches
  // for). Each direction arms at STICK_ON and releases at STICK_OFF.
  if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
    bool up = event.caxis.value < -(stick_up_ ? STICK_OFF : STICK_ON);
    bool down = event.caxis.value > (stick_down_ ? STICK_OFF : STICK_ON);
    if (up && !stick_up_) {
      if (screen_ == CodeEntry) picker_move(0, -1);
      else if (screen_ == Choose && selection_ > 0) selection_--;
    }
    if (down && !stick_down_) {
      if (screen_ == CodeEntry) picker_move(0, 1);
      else if (screen_ == Choose && selection_ < 1) selection_++;
    }
    stick_up_ = up;
    stick_down_ = down;
  } else if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
    bool l = event.caxis.value < -(stick_left_ ? STICK_OFF : STICK_ON);
    bool r = event.caxis.value > (stick_right_ ? STICK_OFF : STICK_ON);
    if (l && !stick_left_ && screen_ == CodeEntry) picker_move(-1, 0);
    if (r && !stick_right_ && screen_ == CodeEntry) picker_move(1, 0);
    stick_left_ = l;
    stick_right_ = r;
  } else if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
    // Plain ±8000 edge like the menu's RT confirm: triggers spring fully
    // back to zero, so they don't hover near a threshold like a stick.
    bool pressed = event.caxis.value > 8000;
    if (pressed && !rt_active_) controller_confirm();
    rt_active_ = pressed;
  }
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
      // Re-summon a dismissed soft keyboard (touch; no-op elsewhere).
      floating_kb_up_ = code_entry_keyboard(true);
      break;
    case RoomHost:
      if (kShareBand.contains(nx, ny) && !room_code_.empty() &&
          net_share_available())
        net_share_text("Join my Newtonia game! Room code: " + room_code_);
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
