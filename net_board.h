#ifndef NET_BOARD_H
#define NET_BOARD_H

// Leaderboard client seam (LEADERBOARD.md L2) — a sibling of NetSignal:
// a thin WebSocket client for the board worker (board/), speaking the
// /board protocol (JSON control frames + binary chunks). One NetBoard is
// one connection; the worker budgets messages per connection (2 submits,
// 120 queries, 5 fetches), which every game flow fits inside.
//
//   game over : qualify() -> Event::Qualify -> submit() -> Event::Placed
//   menu      : top() + rank_of() -> Event::Top / Event::RankOf;
//               fetch() -> Event::FetchDone (replay saved to dest_path)
//
// Backends mirror the signal seam: native = libdatachannel's WebSocket
// (net_board_rtc.cpp, NEWTONIA_NET_RTC); create() returns nullptr
// everywhere else — INCLUDING web, deliberately: no leaderboard on web in
// the first release (LEADERBOARD.md), and every UI entry point gates on
// create() succeeding, so the feature is simply absent there.
//
// Threading model matches net_signal_rtc.cpp: library callbacks only
// enqueue; poll() parses and drives the transfer state machine on the
// main thread.

#include <stdint.h>

#include <string>
#include <vector>

class NetBoard {
 public:
  struct Row {
    int rank = 0;
    std::string name;        // attested display name ("" = badge-only)
    uint8_t platform = 0;    // NetPlatform tag
    bool verified = false;   // the NAME is platform-attested
    uint32_t score = 0;
    uint32_t generation = 0;
    uint32_t duration_ms = 0;
    uint64_t date = 0;       // submitted_at, epoch ms
    bool has_replay = false; // false = demoted to score-only
    std::string run_id;      // decimal string, the fetch key
  };

  struct Event {
    enum Kind {
      Qualify,   // place, cutline (-1 = board not full), would_place
      Placed,    // place = final rank after upload
      Top,       // rows
      RankOf,    // place
      FetchDone, // replay written to the dest_path passed to fetch()
      Error,     // reason = worker err frame ("unverified", "not-best",
                 // "already-submitted", "rate-limited", ...) or local
                 // ("file", "protocol")
      Closed,    // socket closed / connect failed
    };
    Kind kind;
    int place = 0;
    int players = 0;    // Qualify/RankOf: the board the answer is for (echoed
                        // by the worker), so a stale answer from a board the
                        // client has since flipped away from can be dropped
    long cutline = -1;
    bool would_place = false;
    std::string reason;
    std::vector<Row> rows;
  };

  virtual ~NetBoard() {}

  // Open the socket. Ops may be called immediately after — they queue
  // until the socket is up.
  virtual void connect(const std::string &url) = 0;

  virtual void qualify(const std::string &season, int players,
                       uint32_t score) = 0;
  virtual void top(const std::string &season, int players, int count) = 0;
  virtual void rank_of(const std::string &season, int players,
                       uint32_t score) = 0;
  // Upload the .nrp at `path` (read on the calling thread). The identity
  // triple matches NetSignal::send_identity: platform tag, display name,
  // verification credential — the worker REQUIRES the credential to
  // verify (no unattested replays; FAKE_VERIFY relays excepted).
  virtual void submit(const std::string &path, uint8_t platform,
                      const std::string &name, const std::string &cred) = 0;
  // Download a replay into dest_path (written on the polling thread).
  virtual void fetch(const std::string &season, const std::string &run_id,
                     const std::string &dest_path) = 0;

  // Progress of the active upload/download in percent, -1 when idle.
  virtual int transfer_pct() const = 0;

  // Pops one event (main thread only); false when nothing is pending.
  virtual bool poll(Event &ev) = 0;

  virtual void close() = 0;

  // Platform backend, or nullptr where the leaderboard is unavailable
  // (netless builds, web in v1). Caller owns the result.
  static NetBoard *create();
};

// Board worker URL: NEWTONIA_BOARD_URL env var when set, else the baked
// default (compile-time overrides mirror net_signal_url's:
// -DNEWTONIA_BOARD_BETA=1 or -DNEWTONIA_BOARD_URL_DEFAULT='"wss://..."').
std::string net_board_url();

// Whether the leaderboard feature exists for this build/session: a backend
// exists and online play is allowed. The LEADERBOARD menu row and the
// viewing paths gate on this.
bool net_board_available();

// Whether this build can SUBMIT (net_board_available AND a verification
// backend is present). The upload UI — the game-over prompt and the
// UPLOAD BEST RUN action — gates on this, since the worker refuses any
// unattested submission (LEADERBOARD.md). A viewer-only build sees the
// board but no upload affordances.
bool net_board_can_submit();

// The verification credential to SEND with a submission — normally
// net_local_verify_credential(), but overridable by NEWTONIA_BOARD_TEST_CRED
// for headless testing against a FAKE_VERIFY worker (a build with no real
// verify backend otherwise cannot exercise the upload path).
std::string net_board_verify_credential();

// PEEK the current verification credential without triggering a re-mint
// (net_local_verify_credential_peek, or the test-credential simulator). The
// upload retry polls this to detect a genuinely fresh credential after the
// first submit's read already fired the next mint — never minting per poll.
std::string net_board_verify_credential_peek();

// Strip a worker-controlled wire string to printable 7-bit ASCII, capped
// at max_len. Every string off the board socket (err reasons, run_ids)
// that reaches a log or the font must pass through here first — the socket
// disables TLS verification, so a hostile server is in scope (the same
// control-byte/log-injection boundary net_sanitize_name enforces on names).
std::string net_board_sanitize(const std::string &s, size_t max_len = 64);

// ---- shared frame helpers (used by the backends; exposed for tests) -----
namespace NetBoardProto {
std::string qualify_frame(const std::string &season, int players,
                          uint32_t score);
std::string top_frame(const std::string &season, int players, int count);
std::string rank_of_frame(const std::string &season, int players,
                          uint32_t score);
std::string submit_frame(size_t size, uint8_t platform,
                         const std::string &name, const std::string &cred);
std::string submit_end_frame();
std::string fetch_frame(const std::string &season, const std::string &run_id);
// Parse one worker JSON frame into an Event. Returns false for frames the
// protocol doesn't know (skip and keep draining) — fetch-ok/fetch-end and
// submit-ok are handled inside the backend's transfer state machine, not
// surfaced here.
bool parse_frame(const std::string &frame, NetBoard::Event &ev);
// Internal frames the backend state machine consumes.
bool is_submit_ok(const std::string &frame);
bool fetch_ok_size(const std::string &frame, size_t *size_out);
bool is_fetch_end(const std::string &frame);
}  // namespace NetBoardProto

#endif /* NET_BOARD_H */
