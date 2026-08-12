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

#include <map>

#include <set>
#include <string>
#include <vector>

#include "net_lan.h"
#include "net_session.h"
#include "state.h"

class NetSession;
class NetTransport;
class NetSignal;

class NetLobby : public State {
public:
  NetLobby();
  virtual ~NetLobby();

  // True while the room-code field is consuming typed characters — the
  // platform layer suppresses global single-key hotkeys (the bare F
  // fullscreen toggle) so typing into the field can't trigger them
  // (StateManager::text_entry_active).
  bool code_entry_active() const { return screen_ == CodeEntry; }

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
    LobbyFailed,     // error headline; ENTER -> back to the HOST/JOIN chooser
  };

  void confirm();
  // The single lobby decision ladder off the CodeEntry screen: w/s pick
  // HOST/JOIN, Enter/space confirm, Esc leaves, V/C paste/copy. Fed by
  // keyboard_up (after touch/CodeEntry filtering) and by controller() via
  // State::nav_key_from_controller, so pad and keyboard behave identically.
  void nav_input(unsigned char key);
  void start_paste();
  void handle_paste(const std::string &blob);
  void copy_local_description();
  void reset_to_choose();
  void leave_to_menu();
  // Platform policy forbids online play (net_policy.h): shared refusal for
  // every lobby commit point (interactive HOST/JOIN and the rejoin/invite
  // ctor). Sets LobbyFailed with policy_blocked_, whose TRY AGAIN exits to
  // the menu — re-offering the HOST/JOIN chooser would just refuse again.
  void fail_online_not_allowed();
  // show_ms <= 0 = the default 4 s; known-error advisories pass longer.
  void set_status(const char *text, int show_ms = -1);
  // Summon (open=true) / dismiss the code-entry keyboard: the touch soft
  // keyboard, or the Steam Deck floating keyboard (returns true when the
  // floating keyboard actually came up). A programmatic dismiss issued
  // while the keyboard is believed up is counted, so its async
  // FloatingGamepadTextInputDismissed_t callback is absorbed by the tick
  // drain instead of being mistaken for a USER dismiss (which would clear
  // floating_kb_up_ while a re-summoned keyboard is still visible — the
  // rate-limit bounce dismissed-then-reshowed within one frame; Glenn).
  bool code_entry_keyboard(bool open);
  void pump_signal(int delta);
  void fall_back_to_manual(const char *why);
  // The joiner's twin of the host's manual fallback: the signal server
  // never answered, but a joiner can't do anything by itself — fail to
  // LobbyFailed honestly instead of stranding it on the paste screen.
  void join_unreachable(const char *why);
  void code_entry_key(unsigned char key);
  void schedule_rejoin_retry(const char *why, int delay_ms);
  // A rejoin that got as far as a live session (handshake done) but then had
  // its transport flap — common in the first seconds after a network returns.
  // In rejoin mode with budget left, tear the dead session down and re-enter
  // the retry loop instead of failing terminally. Returns true if it did;
  // false means this isn't a retryable rejoin and the caller should fail.
  bool rejoin_retry_after_session_loss(const char *why);
  // Controller support (Steam Deck / gamepad-only setups): CodeEntry
  // shows a character-picker grid of the code alphabet once a controller
  // is seen — d-pad/stick moves, A or right trigger types, B deletes.
  void picker_move(int dx, int dy);
  // Shared picker-grid + LAN-row navigation in logical wasd, fed by the
  // controller translator AND the keyboard arrows (raw 128+specials —
  // nav_key's wasd outputs are code letters, so CodeEntry can't use it).
  void picker_nav(unsigned char key);
  void controller_confirm();  // A / right trigger
  void draw_picker();
  // LAN play (net_lan.h; NETPLAY.md "LAN is not a mode"): the host's
  // lobby always beacons + serves the manual INVITE blob over TCP as a
  // second door beside the relay; the joiner's CodeEntry lists beaconing
  // hosts above the code field. Whichever door's joiner completes first
  // takes the co-op slot.
  void lan_host_update(int delta);
  void lan_join_update(int delta);
  void lan_join_selected();
  void lan_teardown();
  // Round 3: every input flavour reaches the LAN rows. Controller walks
  // down off the picker grid into them; the caps keep the highlight on
  // rows that are actually drawn (2 under the picker, 3 keyboard-flow).
  bool picker_on_bottom_row() const;
  int lan_rows_shown() const;
  void lan_rejoin_restart(const char *why);

  // ---- B4b waiting room (PB-D6) ----------------------------------------
  // Active only while HOSTING with net_seat_cap() > 2 (every shipping
  // build since the B7 flip): joiners pair one transport/session per worker jid
  // and Ready sessions sit in seated_ until START GAME (Enter/pad-A on the
  // room screen) or until the room fills, which starts automatically. At
  // cap 2 every waiting-room path is cold and the classic pair-then-enter
  // flow runs byte-for-byte unchanged.
  bool waiting_room() const;
  // One joiner mid-pairing. The transport is owned here until its answer
  // arrives and a session takes it; the session is owned until seated.
  struct PendingJoiner {
    NetTransport *transport = nullptr;
    NetSession *session = nullptr;
    bool offer_sent = false;
    int seat = 0;      // reserved when the session forms (WELCOME is fixed)
    bool lan = false;  // came through the LAN door (synthetic key, no jid)
  };
  std::map<std::string, PendingJoiner> pending_;  // keyed by jid / "lan#N"
  struct SeatedPeer {
    NetSession *session;
    std::string jid;  // "" for LAN-door peers
    int seat;
    // Captured from jid_attested_ at seating time: a seated peer whose
    // SIGNAL socket later drops keeps its badge (the p2p link is what
    // seats it; PeerLeave erases the jid_attested_ entry).
    NetIdentity attested;
  };
  std::vector<SeatedPeer> seated_;
  // Waiting-room roster selection (host, FOURPLAYER.md O3). -1 = no row
  // picked, which is the resting state: confirm then means START GAME,
  // exactly as before this existed. 0..n-1 pick a seated peer, whose
  // confirm ARMS a kick and whose second confirm performs it — one press
  // must not end someone's game, and Enter is also the start key.
  int host_sel_ = -1;
  int host_kick_armed_ = -1;
  void host_kick_selected();
  int lan_door_serial_ = 0;  // mints the synthetic "lan#N" pending keys
  int next_free_seat() const;         // lowest free 2..cap; 0 = room full
  void waiting_room_update(int delta);  // pump handshakes + seated liveness
  void waiting_room_start();          // hand every seated session to GLGame
  void drop_pending(const std::string &key, const char *why);
  void teardown_waiting_room();       // delete everything not handed off
public:
  // M3-1 auto-rejoin: skip Choose and join the known room immediately.
  explicit NetLobby(const std::string &rejoin_code);
  // LAN rejoin (round 4): the session came through the LAN door, so the
  // way back is rediscovery, not a room code — browse for the remembered
  // host NAME and auto-run the blob exchange when its beacon reappears
  // (the host's GLGame re-beacons on loss; a restarted host app beacons
  // the same name from its lobby). The tag disambiguates from the
  // room-code ctor above.
  struct LanRejoinTag {};
  NetLobby(const std::string &lan_host_name, LanRejoinTag);
  // A room known to be dead (host said BYE, or the relay reported it
  // closed/gone): the clipboard auto-join refuses it for the rest of this
  // run — typing it manually still works.
  static void mark_room_dead(const std::string &code);

  // Kick bans, for the host process's lifetime (FOURPLAYER.md O3). Keyed on
  // the pilot's IDENTITY — name + platform — because a jid is minted per
  // socket and a room code is what they already have; identity is the only
  // thing that survives a reconnect. Static (not per-lobby, not per-game)
  // so a ban set in the waiting room still holds once the game starts and
  // the peer tries the mid-game rejoin door, and vice versa.
  //
  // A NAMELESS peer cannot be banned: there is nothing to key on, and
  // matching on platform alone would lock out every desktop player. The
  // kick still works — they just aren't kept out.
  static void ban_identity(const NetIdentity &id);
  static bool identity_banned(const NetIdentity &id);
  static void clear_bans();  // hosting a NEW room starts with a clean slate
private:

  Screen screen_;
  int selection_;  // Choose: 0 = HOST, 1 = JOIN, 2 = BACK TO MENU band
  // CodeEntry's controller picker layout (grid + compact rows under it)
  // vs the keyboard layout — one flag for the draw and the mouse
  // hit-test, so the two can't disagree about which rows are on screen.
  bool code_entry_grid() const;
  // Nav keys pressed (key-DOWN) while THIS lobby was on screen: a confirm
  // release in keyboard_up acts only if its press is in here — the
  // board_prompt_pressed_ / net_card_pressed_ pattern. The auto-rejoin
  // hand-off arrives mid-fight, so a fire key (space IS a confirm) held
  // through the disconnect releases into this lobby; per-instance, so a
  // key pressed before the lobby existed is stale by construction.
  std::set<unsigned char> nav_pressed_;
  // Lifetime of this lobby (ticked in tick): a rejoin lobby ignores taps
  // on the exit band for its first moments — on touch the fire zone
  // overlaps that strip, and a fire-mash tail arriving with the hand-off
  // must not abandon the rejoin (see touch_tap).
  int age_ms_ = 0;
  static const int kRejoinTapGraceMs = 700;
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
  // The pending read is an explicit paste (controller X): user intent,
  // so the own-room auto-join guards don't apply, and an empty/invalid
  // clipboard gets told off instead of silently ignored.
  bool code_clip_explicit_ = false;
  // The in-flight join came from the clipboard AUTO-join, not the
  // player: its relay failure stays silent (dead-marking still stops
  // retries). A typed bad code followed ~800 ms later by the repoll
  // probing a stale clipboard code showed TWO "NO ROOM WITH THAT CODE"
  // errors — reading as the lobby retrying (Glenn, Deck, task #165).
  bool last_join_was_auto_ = false;
  int code_clip_retry_ms_;   // Android 10 focus-gated reads: brief retry
  int code_clip_repoll_ms_ = 0;  // desktop: idle re-poll for late codes/invites
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
  // This LobbyFailed came from the online-play policy gate: the fail
  // screen's confirm leaves to the menu instead of the HOST/JOIN chooser.
  bool policy_blocked_ = false;
  // Joiner: time on the RoomJoining screen. A room can be joined while
  // hostless (reclaim grace) — if no host ever offers, fail instead of
  // showing "JOINING THE ROOM" forever.
  int join_wait_ms_ = 0;
  // The clipboard auto-join is probing a code that matches the persisted
  // last-hosted pref: maybe a live sibling instance on this machine,
  // maybe our own kill-orphaned room — the no-offer timeout is short.
  bool own_room_probe_ = false;

  // Peer attestation from the signaling worker (NETPLAY.md V0). The worker
  // verifies each side's platform credential (Steam Web-API ticket, etc.)
  // and broadcasts an `identity` message; we fold the peer's attested fields
  // in here and hand it to the GLGame at construction. Empty until the
  // `identity` message arrives (or forever, on a legacy/unverified peer).
  NetIdentity attested_peer_;
  // Multi-join worker scoping (B3): the room admits several joiners, so a
  // hosting lobby keys attestations by the announcing jid (the frame's
  // `from` stamp) and only the jid whose ANSWER we adopt becomes
  // attested_peer_ — a third joiner's badge must never overwrite the
  // paired peer's. Legacy frames (no `from` — the pre-multi-join worker,
  // which only ever relays the paired joiner) fold directly as before.
  std::map<std::string, NetIdentity> jid_attested_;
  std::string paired_jid_;  // the jid whose answer created session_
  // A signaling worker was actually used (room-code flow) — the session is
  // ONLINE-strict for identity display. The manual clipboard fallback clears
  // this (worker-less = OFFLINE, the peer's claimed name renders). Set true
  // on the first Room/Joined event, false in fall_back_to_manual.
  bool used_worker_ = false;
  // The lobby handed its session to a GLGame (request_state_change). The
  // dtor then leaves the warmed verification credential alone — the game
  // needs it for host-reclaim re-attests, and releasing here would cancel
  // a ticket the worker may still be validating. Only a lobby that ends the
  // netplay chain (backed out to the menu) mops the credential up.
  bool handed_off_to_game_ = false;
  // Announce our identity to the worker; re-sent on every Room/Joined event
  // (each fresh socket, including a host reclaim), which the worker re-attests.
  void send_local_identity();
  // Consume a worker `identity` broadcast describing the peer.
  void apply_peer_attestation(uint8_t platform, const std::string &name,
                              bool verified);

  // Controller state (stick/trigger edges live in State's shared nav
  // translator; only the picker's own bits remain here).
  bool controller_seen_ = false;  // draws the picker + button hints
  // The Deck's floating keyboard is showing: hide the picker under it.
  // Cleared by the Steamworks dismissed callback (instant), or by a
  // controller event reaching us (fallback — the keyboard consumes
  // controller input while up, so an event proves it was dismissed).
  bool floating_kb_up_ = false;
  // This hardware has actually shown the floating keyboard (Deck):
  // enables the Y - KEYBOARD re-summon and its hint. Steam has no
  // keyboard-SHOWN callback, so a Steam+X summon is invisible to the
  // game — Y routes the re-summon through a path it can see.
  bool floating_kb_available_ = false;
  // Programmatic floating-keyboard dismisses awaiting their async
  // Dismissed callback: the tick drain decrements this instead of
  // clearing floating_kb_up_, so only a genuine user dismiss brings the
  // picker/hints back (see code_entry_keyboard).
  int floating_kb_dismiss_pending_ = 0;
  int picker_index_ = 0;

  // LAN state. lan_transport_ is the host's LAN-door peer connection
  // (no STUN, non-trickle) — the relay keeps its own transport_ so an
  // SDP is never answered twice across the two doors.
  NetLan::Announce lan_announce_;
  NetLan::Browse lan_browse_;
  NetTransport *lan_transport_ = nullptr;
  bool lan_offer_set_ = false;   // offer blob handed to the announcer
  int lan_sel_ = -1;             // CodeEntry host row (-1 = code field)
  bool lan_joining_ = false;     // joiner committed to a LAN host
  std::string lan_host_name_;    // the host we committed to (for draw)
  std::string lan_beacon_name_;  // host: the name our beacon advertises
  // Rejoin wait screen showing selectable host rows (nothing committed).
  bool lan_rejoin_browsing() const;
  int lan_browse_ms_ = 0;        // time browsing (blob-pickup hold grace)
  bool lan_blob_held_ = false;   // logged the hold once (repoll re-fires it)
  bool lan_rejoin_ = false;      // round 4: rediscover-and-rejoin mode
  std::string lan_rejoin_name_;  // the host name to auto-select

  int currentTime;
  WrappedPoint viewpoint;
  GLStarfield *starfield;
};

#endif /* NET_LOBBY_H */
