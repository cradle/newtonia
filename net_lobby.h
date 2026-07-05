#ifndef NET_LOBBY_H
#define NET_LOBBY_H

// Online co-op lobby — see NETPLAY.md. Reached from the menu's ONLINE row
// (shown only when net_available()).
//
// Primary flow (Milestone 2): room codes through the signal worker.
//   HOST: a room is created and its 4-letter code shown on screen ->
//         tell your friend the code -> everything else is automatic.
//   JOIN: type the code -> the offer/answer relay is automatic.
//
// Fallback (Milestone 1): if the signal server is unreachable the lobby
// drops to the manual clipboard flow — offer/answer codes copied and
// pasted by hand (V pastes, C re-copies).
//
// Once the transport connects, NetSession runs HELLO/WELCOME; the host
// enters GLGame immediately and the joiner bootstraps from snapshot #1.
// Keyboard and controller (code entry is keyboard-only) — the menu hides
// ONLINE in touch mode.

#include <SDL.h>

#include <string>
#include <vector>

#include "net_session.h"
#include "state.h"

class NetSession;
class NetTransport;
class NetSignal;

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
    RoomHost,        // room created (or being created); code on screen
    CodeEntry,       // joiner types the 4-letter room code
    RoomJoining,     // code sent; waiting for the room / host's offer
    HostGathering,   // manual fallback: waiting for the local offer
    HostWaitAnswer,  // manual fallback: offer copied; V pastes the answer
    JoinWaitOffer,   // manual fallback: V pastes the host's offer
    JoinGathering,   // manual fallback: waiting for the local answer
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
  void pump_signal(int delta);
  void fall_back_to_manual(const char *why);
  void code_entry_key(unsigned char key);

  Screen screen_;
  int selection_;  // Choose: 0 = HOST, 1 = JOIN
  bool hosting_;

  NetTransport *transport_;  // owned until handed to session_
  NetSession *session_;      // owned
  NetSignal *signal_;        // owned; null in manual fallback
  std::string room_code_;    // host: assigned code
  std::vector<std::string> ice_servers_;  // TURN triples from the relay
  std::string code_entry_;   // joiner: code being typed
  bool offer_sent_;          // host: offer pushed to the room
  bool answer_sent_;         // joiner: answer pushed to the room
  int signal_wait_ms_;       // time waiting on the signal server

  bool paste_pending_;
  std::string paste_buffer_;
  int connect_wait_ms_;  // time in WaitConnect, for the no-relay timeout
  Net::SnapshotAssembler assembler_;  // joiner: reassembles snapshot #1

  std::string status_;  // transient hint / error line
  int status_ms_;

  int currentTime;
  WrappedPoint viewpoint;
  GLStarfield *starfield;
};

#endif /* NET_LOBBY_H */
