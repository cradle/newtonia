#include "net_lobby.h"

#include <vector>

#include "gl_compat.h"
#include "glstarfield.h"
#include "mat4.h"
#include "menu.h"
#include "net_session.h"
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

}  // namespace

NetLobby::NetLobby()
    : screen_(Choose),
      selection_(0),
      hosting_(false),
      transport_(nullptr),
      session_(nullptr),
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
    if (hosting_) {
      transport_->start_host();
      screen_ = HostGathering;
    } else {
      screen_ = JoinWaitOffer;
    }
  } else if (screen_ == LobbyFailed) {
    reset_to_choose();
  }
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
  if (screen_ == JoinWaitOffer) {
    if (kind == 'O') {
      transport_->start_join(sdp);
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
  net_clipboard_write(Net::encode_signal(hosting_, sdp));
  set_status("COPIED TO CLIPBOARD");
}

void NetLobby::tick(int delta) {
  currentTime += delta;
  viewpoint += Point(1, 0) * (0.025 * delta);
  if (viewpoint.x() > WORLD_W) viewpoint += Point(-WORLD_W, 0);
  if (status_ms_ > 0) status_ms_ -= delta;

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
        screen_ = Connected;
      } else if (p == NetSession::Rejected) {
        set_status(session_->reject_reason() == NetSession::RejectVersionMismatch
                       ? "VERSION MISMATCH - UPDATE BOTH GAMES"
                       : "HOST REFUSED THE CONNECTION");
        screen_ = LobbyFailed;
      } else if (p == NetSession::Failed ||
                 connect_wait_ms_ > CONNECT_TIMEOUT_MS) {
        set_status("COULD NOT CONNECT - NETWORK MAY NEED A RELAY (M2)");
        screen_ = LobbyFailed;
      }
      break;
    }

    case Connected:
      session_->update(delta);
      if (session_->phase() == NetSession::Failed) {
        set_status("CONNECTION LOST");
        screen_ = LobbyFailed;
      }
      break;

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
      lines.push_back("HOST MAKES AN INVITE CODE");
      lines.push_back("JOIN PASTES ONE FROM A FRIEND");
      break;
    }
    case HostGathering:
      lines.push_back("PREPARING YOUR INVITE CODE");
      if (blink) lines.push_back("PLEASE WAIT...");
      break;
    case HostWaitAnswer:
      lines.push_back("INVITE CODE COPIED TO CLIPBOARD");
      lines.push_back("SEND IT TO YOUR FRIEND");
      lines.push_back("");
      lines.push_back("V - PASTE THEIR REPLY CODE");
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
        lines.push_back("CONNECTING...");
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
      lines.push_back("CONNECTED!");
      lines.push_back(session_ && session_->player_id() == 1
                          ? "YOU ARE PLAYER 1 (HOST)"
                          : "YOU ARE PLAYER 2");
      lines.push_back("");
      lines.push_back("ONLINE PLAY ARRIVES IN A LATER PHASE");
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

void NetLobby::keyboard_up(unsigned char key, int x, int y) {
  (void)x;
  (void)y;
  if (key == 27) {
    leave_to_menu();
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
