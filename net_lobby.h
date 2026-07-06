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
// Keyboard and controller throughout — code entry types on a keyboard or
// walks an on-screen character picker with a controller (Steam Deck has
// neither touch nor a soft keyboard in game mode). The menu hides ONLINE
// in touch mode.

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
  void touch_tap(float nx, float ny) override;

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
  void schedule_rejoin_retry(const char *why, int delay_ms);
  void retry_join();  // abandon current attempt -> empty join screen
  // Controller support (Steam Deck / gamepad-only setups): CodeEntry
  // shows a character-picker grid of the code alphabet once a controller
  // is seen — d-pad/stick moves, A or right trigger types, B deletes.
  void picker_move(int dx, int dy);
  void controller_confirm();  // A / right trigger
  void draw_picker();
public:
  // M3-1 auto-rejoin: skip Choose and join the known room immediately.
  explicit NetLobby(const std::string &rejoin_code);
  // A room known to be dead (host said BYE, or the relay reported it
  // closed/gone): the clipboard auto-join refuses it for the rest of this
  // run — typing it manually still works.
  static void mark_room_dead(const std::string &code);
private:

  Screen screen_;
  int selection_;  // Choose: 0 = HOST, 1 = JOIN
  bool hosting_;

  NetTransport *transport_;  // owned until handed to session_
  NetSession *session_;      // owned
  NetSignal *signal_;        // owned; null in manual fallback
  std::string room_code_;    // host: assigned code
  std::string room_token_;   // host: reclaim proof (M3-1)
  std::vector<std::string> ice_servers_;  // TURN triples from the relay
  std::string code_entry_;   // joiner: code being typed
  bool offer_sent_;          // host: offer pushed to the room
  bool answer_sent_;         // joiner: answer pushed to the room
  int signal_wait_ms_;       // time waiting on the signal server

  bool paste_pending_;
  std::string paste_buffer_;
  bool code_clip_pending_;   // CodeEntry: auto-join poll of the clipboard
  int code_clip_retry_ms_;   // Android 10 focus-gated reads: brief retry
  // M3-1 auto-rejoin retries: a mobile client's own network is often still
  // down when the rejoin fires; retry the relay instead of falling back
  // to the manual screen, within a budget matching the room grace window.
  bool rejoin_mode_;
  int rejoin_retry_ms_;      // >0: next attempt countdown
  int rejoin_budget_ms_;     // total time before giving up
  int connect_wait_ms_;  // time in WaitConnect, for the no-relay timeout
  Net::SnapshotAssembler assembler_;  // joiner: reassembles snapshot #1

  std::string status_;  // transient hint / error line
  int status_ms_;
  // LobbyFailed headline; "SERVER SHUT DOWN" when the host deliberately
  // closed the room (worker err "host-closed").
  std::string fail_headline_ = "SOMETHING WENT WRONG";
  // Joiner: time on the RoomJoining screen. A room can be joined while
  // hostless (reclaim grace) — if no host ever offers, fail instead of
  // showing "JOINING THE ROOM" forever.
  int join_wait_ms_ = 0;

  // Controller state: left-stick / right-trigger edge detection (the
  // menu's pattern) and the CodeEntry picker selection.
  bool stick_up_ = false, stick_down_ = false;
  bool stick_left_ = false, stick_right_ = false;
  bool rt_active_ = false;
  bool controller_seen_ = false;  // draws the picker + button hints
  int picker_index_ = 0;

  int currentTime;
  WrappedPoint viewpoint;
  GLStarfield *starfield;
};

#endif /* NET_LOBBY_H */
