#ifndef MENU_H
#define MENU_H

#include <SDL.h>
#include <string>
#include <vector>
#include "state.h"
#include "savegame.h"
#include "net_board.h"

class Menu : public State {
public:
  Menu();
  virtual ~Menu();

  void draw() override;
  void keyboard(unsigned char key, int x, int y) override;
  void keyboard_up(unsigned char key, int x, int y) override;
  void controller(SDL_Event event) override;
  void tick(int delta) override;
  void touch_tap(float nx, float ny) override;
  bool back_pressed() override;

private:
  // The single menu decision ladder: w/s move, a/d adjust (options),
  // Enter/space confirm, Esc backs out one level. Fed by keyboard_up (after
  // per-platform touch filtering) and by controller() via the shared
  // State::nav_key_from_controller translation, so keyboard and pad behave
  // identically on every menu screen. src is the pad that produced a
  // confirm (bound to player 1), null for real keyboard input.
  void nav_input(unsigned char key, SDL_GameController *src);
  void confirm_selection(SDL_GameController *ctrl);
  int  max_menu_items() const;
  // RESUME HOSTING row (NETPLAY.md host process-death resume): shown at
  // the top when a fresh NetResume ticket + online save survived a killed
  // hosting process and the room's reclaim grace may still be open.
  // Expiry, or picking any other way into a game, deletes both files.
  int  base_menu_rows() const;  // rows above ONLINE: resume?/continue?/new
  int  continue_row_index() const;  // -1 when hidden
  void decline_net_resume();
  void scan_net_resume();
  // ONLINE row (netplay lobby): only on builds with a net backend.
  bool show_online_row() const;
  int  online_row_index() const;  // -1 when the row is hidden
  // OPTIONS row: always shown. The options screen works on keyboard,
  // controller, and touch (tap a row to cycle its value).
  bool show_options_row() const;
  int  options_row_index() const;  // -1 when hidden
  // REPLAYS row (REPLAY.md R3): shown only while at least one .nrp exists.
  // The list screen offers one row per existing file — CURRENT RUN
  // (live/resumable offline), LAST RUN (most recently completed offline),
  // ONLINE RUN (the most recent online session, ended or abandoned) and
  // BEST RUN — with score/level/date; files this build can't parse render
  // as unselectable rows naming the reason (damaged / newer version).
  // Selecting a row starts R2 playback.
  bool show_replays_row() const { return !replay_rows_.empty(); }
  int  replays_row_index() const;  // -1 when hidden
  void scan_replays();             // rebuild replay_rows_ from disk
  void open_replays();
  // LEADERBOARD row/screen (LEADERBOARD.md L3): shown only when the build
  // has a NetBoard backend and online play is allowed. The list screen
  // fetches the current season's top rows (SOLO/CO-OP toggle on the first
  // selectable row), shows the player's own standing (rank-of footer, or
  // " - YOU" on their visible row), lets a row with a stored replay be
  // downloaded and watched, and carries the UPLOAD BEST RUN retry action.
  bool show_board_row() const;
  int  board_row_index() const;    // -1 when hidden
  void open_board();
  void close_board();
  void board_request();            // (re)fetch top + rank-of for board_players_
  void board_poll();               // drive NetBoard events (from tick)
  void board_nav_confirm();        // confirm on the current selection
  bool board_transfer_busy() const;// a fetch or upload is in flight
  int  board_entry_count() const;  // toggle row + score rows + upload row
  bool board_upload_row_shown() const;
  bool board_best_on_board() const;   // local best is a visible row
  const char *board_upload_status_text() const;  // phase-3 label
  void board_start_upload();
  struct ReplayRow {
    std::string label;   // CURRENT RUN / LAST RUN / ONLINE RUN / BEST RUN
    std::string path;
    bool ok;             // readable by this build (status == HEADER_OK)
    // Why not, when !ok — the row prints this instead of score/level/date,
    // so a damaged file and one from a newer build read differently.
    int status;          // Replay::HeaderStatus
    uint32_t score, level;
    std::string date;    // YYYY-MM-DD from the header's creation date
  };
  // The unselectable row's text for a status that isn't HEADER_OK.
  static const char *replay_status_text(int status);
  // Shared vertical row layout for the main menu (desktop cursor rows and
  // touch tap targets use the same geometry).
  static int menu_row_size();
  void draw_menu_rows(const std::vector<std::string> &rows);
  int  menu_row_at(float ny) const;  // -1 when the tap misses every row
  void open_options();
  void close_options();
  void adjust_active_row(int delta, bool wrap = false);

  int currentTime;
  int high_score;
  bool has_save_ = false;
  bool has_net_resume_ = false;      // RESUME HOSTING row shown
  bool net_resume_scanned_ = false;  // ticket checked (first tick, not ctor)
  std::string net_resume_code_;      // its room code, for the row label
  Uint32 net_resume_expire_at_ = 0;  // SDL_GetTicks() when grace runs out
  int  menu_selection = 0;
  bool options_mode_ = false;
  bool replays_mode_ = false;
  int  replay_sel_ = 0;                 // cursor in the replays list
  std::vector<ReplayRow> replay_rows_;  // rebuilt by scan_replays()
  // ---- leaderboard screen state (LEADERBOARD.md L3) ----
  bool board_mode_ = false;
  NetBoard *board_net_ = nullptr;       // owned; non-null while screen open
  int  board_players_ = 1;              // 1 = SOLO board, 2 = CO-OP
  int  board_sel_ = 0;                  // cursor over board_entry_count()
  std::vector<NetBoard::Row> board_rows_;
  bool board_loading_ = false;          // top fetch in flight
  bool board_error_ = false;            // socket closed / worker error
  int  board_your_rank_ = 0;            // rank-of answer (0 = none); a stale
                                        // answer is dropped by matching the
                                        // worker-echoed players field
  std::string board_season_;            // best.nrp's, else this build's
  std::string board_best_run_id_;       // own-row detection (decimal string)
  uint32_t board_best_score_ = 0;       // rank-of query + upload row label
  bool board_best_clean_ = false;       // upload row shown only for a clean best
  int  board_up_phase_ = 0;             // 0 idle, 1 uploading, 2 placed, 3 failed
  int  board_up_rank_ = 0;
  std::string board_up_reason_;
  bool board_fetching_ = false;         // replay download in flight
  int  sensitivity_index_[2] = {2, 2};  // per-player index into SENSITIVITY_VALUES
  int  smoothing_index_[2]   = {1, 1};  // per-player index into SMOOTHING_VALUES (1=NORMAL=0.004)
  int  star_density_index_   = 4;       // index into STAR_DENSITY_MULTIPLIERS (4=full)
  int  camera_index_[2]      = {1, 1};  // per-player: 0=FIXED, 1=ROTATE
  int  auto_record_index_    = 0;       // 0=OFF, 1=ON (Preferences::auto_record_replays)
  int  leaderboard_index_    = 1;       // 0=OFF, 1=ON (Preferences::leaderboard_prompts)
  int  active_row_ = 0;                 // index into the options row list (see opt_row)
  WrappedPoint viewpoint;
  GLStarfield *starfield;
  static const int default_world_width, default_world_height;

  Mix_Music *music = NULL;

  bool quit_confirm_ = false;
  int  quit_selection_ = 0;  // 0 = Yes, 1 = No
  bool new_confirm_ = false; // confirm NEW GAME when it would overwrite a save
  int  new_selection_ = 1;   // 0 = Yes, 1 = No — No is the safe default
  bool attract_mode_ = true; // flashing PRESS ENTER/START (TAP TO START on touch) before the menu
};

#endif
