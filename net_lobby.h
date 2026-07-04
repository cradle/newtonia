#ifndef NET_LOBBY_H
#define NET_LOBBY_H

// Online co-op lobby — see NETPLAY.md. Reached from the menu's ONLINE row
// (shown only when net_available()). Runs the manual clipboard signaling
// flow of Milestone 1:
//
//   HOST: offer is generated and copied to the clipboard -> send it to
//         your friend out-of-band -> press V to paste their answer.
//   JOIN: press V to paste the host's offer -> the answer is copied to
//         the clipboard -> send it back -> wait for the connection.
//
// Once the transport connects, NetSession runs HELLO/WELCOME; the lobby
// then shows CONNECTED (game start is wired up in Phases 6/7). Keyboard
// and controller only in Milestone 1 — the menu hides ONLINE in touch
// mode.

#include <SDL.h>

#include <string>

#include "state.h"

class NetSession;
class NetTransport;

class NetLobby : public State {
public:
  NetLobby();
  virtual ~NetLobby();

  void draw() override;
  void keyboard(unsigned char key, int x, int y) override;
  void keyboard_up(unsigned char key, int x, int y) override;
  void controller(SDL_Event event) override;
  void tick(int delta) override;
  bool back_pressed() override;

private:
  enum Screen {
    Choose,          // HOST / JOIN selection
    HostGathering,   // waiting for the local offer (ICE gathering)
    HostWaitAnswer,  // offer on clipboard; V pastes the answer
    JoinWaitOffer,   // V pastes the host's offer
    JoinGathering,   // waiting for the local answer
    WaitConnect,     // signaling done on our side; transport connecting
    Connected,       // session Ready
    LobbyFailed,     // error text in status_; ENTER retries
  };

  void confirm();
  void start_paste();
  void handle_paste(const std::string &blob);
  void copy_local_description();
  void reset_to_choose();
  void leave_to_menu();
  void set_status(const char *text);

  Screen screen_;
  int selection_;  // Choose: 0 = HOST, 1 = JOIN
  bool hosting_;

  NetTransport *transport_;  // owned until handed to session_
  NetSession *session_;      // owned

  bool paste_pending_;
  std::string paste_buffer_;
  int connect_wait_ms_;  // time in WaitConnect, for the no-relay timeout

  std::string status_;  // transient hint / error line
  int status_ms_;

  int currentTime;
  WrappedPoint viewpoint;
  GLStarfield *starfield;
};

#endif /* NET_LOBBY_H */
