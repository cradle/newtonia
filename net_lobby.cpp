#include "net_lobby.h"

#include <cstdio>
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
#include "typer.h"
#include "view/overlay.h"

namespace {

const int WORLD_W = 5000, WORLD_H = 5000;  // starfield extent (as in Menu)
const int STATUS_SHOW_MS = 4000;

// STUN-only in Milestone 1: if a paste-complete pair can't connect in this
// window they are almost certainly behind symmetric NATs (no relay yet).
const int CONNECT_TIMEOUT_MS = 25000;

// No word from the signal server in this window (room creation / join
// acknowledgement) — treat it as unreachable and use the manual flow.
const int SIGNAL_TIMEOUT_MS = 12000;

}  // namespace

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
      connect_wait_ms_(0),
      status_ms_(0),
      currentTime(0),
      viewpoint(Point(0, WORLD_H / 2)),
      starfield(new GLStarfield(Point(WORLD_W, WORLD_H), star_density_scale())) {}

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
}

void NetLobby::set_status(const char *text) {
  status_ = text;
  status_ms_ = STATUS_SHOW_MS;
}

void NetLobby::reset_to_choose() {
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
  screen_ = Choose;
}

void NetLobby::leave_to_menu() {
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
      } else {
        screen_ = JoinWaitOffer;
      }
    }
  } else if (screen_ == CodeEntry) {
    if (code_entry_.size() == (size_t)NET_ROOM_CODE_LEN) {
      signal_->connect_join(net_signal_url(), code_entry_);
      signal_wait_ms_ = 0;
      screen_ = RoomJoining;
    } else {
      set_status("THE CODE IS 5 LETTERS");
    }
  } else if (screen_ == LobbyFailed) {
    reset_to_choose();
  }
}

// The signal server never answered (or refused the room): keep the pair
// playable through the Milestone 1 clipboard flow.
void NetLobby::fall_back_to_manual(const char *why) {
  printf("[lobby] manual fallback: %s\n", why);
  fflush(stdout);
  if (signal_) {
    signal_->close();
    delete signal_;
    signal_ = nullptr;
  }
  set_status("NO ROOM SERVER - USING MANUAL CODES");
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
  printf("[lobby] pasted %d chars, kind=%c, screen=%d\n",
         (int)blob.size(), kind ? kind : '?', (int)screen_);
  fflush(stdout);
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
  printf("[lobby] copied %s code to clipboard (%d chars)\n",
         hosting_ ? "invite" : "reply", (int)blob.size());
  fflush(stdout);
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
    printf("[lobby] offer sent to room %s\n", room_code_.c_str());
    fflush(stdout);
  }

  // Joiner: answer ready -> push it and start waiting on the transport.
  if (!hosting_ && !answer_sent_ && screen_ == RoomJoining && transport_ &&
      transport_->local_description_ready()) {
    signal_->send_answer(transport_->local_description());
    answer_sent_ = true;
    printf("[lobby] answer sent to room %s\n", room_code_.c_str());
    fflush(stdout);
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
      case NetSignal::Event::Room:
        room_code_ = ev.text;
        printf("[lobby] room %s (%d turn servers)\n", room_code_.c_str(),
               (int)ice_servers_.size());
        fflush(stdout);
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
        break;
      case NetSignal::Event::Offer:
        // Live relay: both ends are online, so the full offer applies and
        // ICE starts on both sides at once (no strip_ice_candidates — that
        // trick belongs to the clipboard flow's human-latency gap).
        if (!hosting_ && screen_ == RoomJoining && transport_) {
          transport_->set_ice_servers(ice_servers_);
          transport_->start_join(ev.text);
        }
        break;
      case NetSignal::Event::Answer:
        if (hosting_ && transport_) {
          transport_->set_remote_answer(ev.text);
          session_ = new NetSession(transport_, NetSession::HostRole);
          transport_ = nullptr;
          connect_wait_ms_ = 0;
          screen_ = WaitConnect;
        }
        break;
      case NetSignal::Event::Error:
        if (screen_ == RoomJoining || screen_ == CodeEntry) {
          if (ev.text == "no-such-room") set_status("NO ROOM WITH THAT CODE");
          else if (ev.text == "room-full") set_status("THAT ROOM IS FULL");
          else if (ev.text == "rate-limited") set_status("TOO MANY TRIES - WAIT A MINUTE");
          else set_status("THE ROOM HAS EXPIRED");
          room_code_.clear();
          code_entry_.clear();
          answer_sent_ = false;
          screen_ = CodeEntry;
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
          fall_back_to_manual("socket closed while joining");
          return;
        }
        // Later screens no longer need the relay; ignore.
        break;
    }
  }

  // Server never answered at all: don't strand the player.
  if ((screen_ == RoomHost || screen_ == RoomJoining) && room_code_.empty()) {
    signal_wait_ms_ += delta;
    if (signal_wait_ms_ > SIGNAL_TIMEOUT_MS)
      fall_back_to_manual("signal server timeout");
  }
}

void NetLobby::tick(int delta) {
  currentTime += delta;
  viewpoint += Point(1, 0) * (0.025 * delta);
  if (viewpoint.x() > WORLD_W) viewpoint += Point(-WORLD_W, 0);
  if (status_ms_ > 0) status_ms_ -= delta;

  pump_signal(delta);

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
    printf("[lobby] transport failed during signaling\n");
    fflush(stdout);
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
            game->net_adopt_signal(signal_, room_code_, ice_servers_);
            signal_ = nullptr;
          }
          request_state_change(game);
          return;
        }
        screen_ = Connected;
      } else if (p == NetSession::Rejected) {
        set_status(session_->reject_reason() == NetSession::RejectVersionMismatch
                       ? "VERSION MISMATCH - UPDATE BOTH GAMES"
                       : "HOST REFUSED THE CONNECTION");
        screen_ = LobbyFailed;
      } else if (p == NetSession::Failed ||
                 (session_->role() == NetSession::HostRole &&
                  connect_wait_ms_ > CONNECT_TIMEOUT_MS)) {
        // Only the host runs the clock here: it pasted the reply, so the
        // connection either establishes within seconds or never will. The
        // JOINER's wait includes the humans ferrying the reply code to the
        // host — minutes, not seconds — so it waits until the transport
        // actually fails (or the player backs out).
        printf("[lobby] connect failed: session_phase=%d waited=%d ms transport_failed=%d\n",
               (int)p, connect_wait_ms_,
               session_->transport() ? (int)session_->transport()->failed() : -1);
        fflush(stdout);
        set_status("COULD NOT CONNECT - NETWORK MAY NEED A RELAY (M2)");
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

  Typer::draw_centered(0, 320, "ONLINE CO-OP", 40);

  const int sz = 18, line = 52;
  int y = 120;
  std::vector<std::string> lines;
  bool blink = (currentTime / 700) % 2 == 0;

  switch (screen_) {
    case Choose: {
      std::string host = std::string(selection_ == 0 ? "> " : "  ") + "HOST";
      std::string join = std::string(selection_ == 1 ? "> " : "  ") + "JOIN";
      Typer::draw_centered(0, 60, host.c_str(), 26);
      Typer::draw_centered(0, -40, join.c_str(), 26);
      lines.push_back("");
      y = -160;
      lines.push_back("HOST MAKES A ROOM CODE");
      lines.push_back("JOIN ENTERS A FRIEND'S CODE");
      break;
    }
    case RoomHost:
      if (room_code_.empty()) {
        lines.push_back("CREATING A ROOM");
        if (blink) lines.push_back("PLEASE WAIT...");
      } else {
        lines.push_back("ROOM CODE");
        Typer::draw_centered(0, 20, room_code_.c_str(), 48);
        y = -100;
        lines.push_back("TELL YOUR FRIEND THE CODE");
        lines.push_back("");
        if (blink) lines.push_back("WAITING FOR PLAYER 2...");
      }
      break;
    case CodeEntry: {
      lines.push_back("ENTER THE ROOM CODE");
      std::string slots;
      for (int i = 0; i < NET_ROOM_CODE_LEN; i++) {
        slots += (size_t)i < code_entry_.size() ? code_entry_[i] : '-';
        if (i + 1 < NET_ROOM_CODE_LEN) slots += ' ';
      }
      Typer::draw_centered(0, 20, slots.c_str(), 48);
      y = -100;
      lines.push_back("TYPE THE CODE YOUR HOST SEES");
      break;
    }
    case RoomJoining:
      lines.push_back("JOINING THE ROOM");
      if (blink) lines.push_back("PLEASE WAIT...");
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
      lines.push_back("SOMETHING WENT WRONG");
      lines.push_back("");
      lines.push_back("ENTER - TRY AGAIN");
      break;
  }

  for (size_t i = 0; i < lines.size(); i++) {
    if (!lines[i].empty())
      Typer::draw_centered(0, y - (int)i * line, lines[i].c_str(), sz);
  }

  if (status_ms_ > 0 && !status_.empty())
    Typer::draw_centered(0, -320, status_.c_str(), 15);

  Typer::draw_centered(0, -420, "ESC - BACK TO MENU", 13, currentTime);
}

void NetLobby::keyboard(unsigned char key, int x, int y) {}

// Room-code typing: letters/digits from the code alphabet, backspace,
// Enter to submit. Runs INSTEAD of the shortcut keys — V, C, W and S are
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
  if (code_entry_.size() < (size_t)NET_ROOM_CODE_LEN &&
      net_room_code_char_ok((char)key)) {
    char c = (char)key;
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    code_entry_ += c;
    // Codes are fixed-length: join the moment the last slot fills.
    if (code_entry_.size() == (size_t)NET_ROOM_CODE_LEN) confirm();
  }
}

void NetLobby::keyboard_up(unsigned char key, int x, int y) {
  (void)x;
  (void)y;
  if (key == 27) {
    leave_to_menu();
    return;
  }
  if (screen_ == CodeEntry) {
    code_entry_key(key);
    return;
  }
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

void NetLobby::controller(SDL_Event event) {
  if (event.type != SDL_CONTROLLERBUTTONDOWN) return;
  switch (event.cbutton.button) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
      if (screen_ == Choose && selection_ > 0) selection_--;
      break;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
      if (screen_ == Choose && selection_ < 1) selection_++;
      break;
    case SDL_CONTROLLER_BUTTON_A:
      confirm();
      break;
    case SDL_CONTROLLER_BUTTON_X:
      start_paste();
      break;
    case SDL_CONTROLLER_BUTTON_Y:
      copy_local_description();
      break;
    case SDL_CONTROLLER_BUTTON_B:
    case SDL_CONTROLLER_BUTTON_BACK:
      leave_to_menu();
      break;
    default:
      break;
  }
}

bool NetLobby::back_pressed() {
  leave_to_menu();
  return true;
}
