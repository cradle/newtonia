// Native leaderboard backend: libdatachannel's built-in WebSocket client,
// exactly the net_signal_rtc.cpp recipe (TLS caveat included). Compiled
// only with NEWTONIA_NET_RTC — an empty translation unit everywhere else.
//
// On top of the socket this runs the /board transfer state machine:
// uploads queue the whole file into the library's send buffer after the
// worker's submit-ok (progress read back from rtcGetBufferedAmount);
// downloads accumulate binary chunks between fetch-ok and fetch-end and
// write dest_path on the main thread. Callbacks only enqueue; poll()
// parses and drives everything on the main thread.

#ifdef NEWTONIA_NET_RTC

#include "net_board.h"

#include <rtc/rtc.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

namespace {

const size_t CHUNK = 60 * 1024;  // < the worker's 64 KB frame expectation

class RtcBoard : public NetBoard {
 public:
  RtcBoard() : ws_(-1), opened_(false), closed_flag_(false) {}
  ~RtcBoard() { close(); }

  void connect(const std::string &url) override {
    close();
    // Same explicit TLS-verification disable as net_signal_rtc.cpp:
    // MbedTLS reaches no OS trust store here, and this socket carries no
    // game-channel security (nothing peer-to-peer rides it).
    rtcWsConfiguration cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.disableTlsVerification = true;
    ws_ = rtcCreateWebSocketEx(url.c_str(), &cfg);
    if (ws_ < 0) {
      closed_flag_ = true;
      return;
    }
    rtcSetUserPointer(ws_, this);
    rtcSetOpenCallback(ws_, &RtcBoard::on_open);
    rtcSetMessageCallback(ws_, &RtcBoard::on_message);
    rtcSetClosedCallback(ws_, &RtcBoard::on_closed);
    rtcSetErrorCallback(ws_, &RtcBoard::on_error);
  }

  void qualify(const std::string &season, int players,
               uint32_t score) override {
    send_text(NetBoardProto::qualify_frame(season, players, score));
  }

  void top(const std::string &season, int players, int count) override {
    send_text(NetBoardProto::top_frame(season, players, count));
  }

  void rank_of(const std::string &season, int players,
               uint32_t score) override {
    send_text(NetBoardProto::rank_of_frame(season, players, score));
  }

  void submit(const std::string &path, uint8_t platform,
              const std::string &name, const std::string &cred) override {
    upload_.clear();
    FILE *fp = fopen(path.c_str(), "rb");
    if (fp) {
      fseek(fp, 0, SEEK_END);
      long len = ftell(fp);
      fseek(fp, 0, SEEK_SET);
      if (len > 0 && len <= 32L * 1024L * 1024L) {
        upload_.resize((size_t)len);
        if (fread(&upload_[0], 1, upload_.size(), fp) != upload_.size())
          upload_.clear();
      }
      fclose(fp);
    }
    if (upload_.empty()) {
      local_error("file");
      return;
    }
    phase_ = UPLOAD_WAIT_OK;
    send_text(NetBoardProto::submit_frame(upload_.size(), platform, name,
                                          cred));
  }

  void fetch(const std::string &season, const std::string &run_id,
             const std::string &dest_path) override {
    fetch_dest_ = dest_path;
    download_.clear();
    download_expect_ = 0;
    phase_ = FETCH_WAIT_OK;
    send_text(NetBoardProto::fetch_frame(season, run_id));
  }

  int transfer_pct() const override {
    if (phase_ == UPLOADING && !upload_.empty() && ws_ >= 0) {
      // Everything is queued into the library's send buffer at once;
      // progress is what has actually left it.
      int buffered = rtcGetBufferedAmount(ws_);
      if (buffered < 0) buffered = 0;
      size_t sent = upload_.size() > (size_t)buffered
                        ? upload_.size() - (size_t)buffered : 0;
      return (int)(sent * 100 / upload_.size());
    }
    if (phase_ == FETCHING && download_expect_ > 0)
      return (int)(download_.size() * 100 / download_expect_);
    return -1;
  }

  bool poll(Event &ev) override {
    // Flush ops queued before the socket opened.
    if (opened_.load()) {
      std::lock_guard<std::mutex> lock(mutex_);
      while (!pending_out_.empty()) {
        const std::string &f = pending_out_.front();
        rtcSendMessage(ws_, f.c_str(), -1);
        pending_out_.pop_front();
      }
    }
    for (;;) {
      Incoming in;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (inbox_.empty()) break;
        in = inbox_.front();
        inbox_.pop_front();
      }
      if (in.binary) {
        if (phase_ == FETCHING) {
          download_.insert(download_.end(), in.bytes.begin(), in.bytes.end());
          if (download_.size() > download_expect_) {
            phase_ = IDLE;
            ev = Event();
            ev.kind = Event::Error;
            ev.reason = "protocol";
            return true;
          }
        }
        continue;
      }
      // Text frame: the transfer state machine first, then the shared parse.
      if (phase_ == UPLOAD_WAIT_OK && NetBoardProto::is_submit_ok(in.text)) {
        for (size_t p = 0; p < upload_.size(); p += CHUNK) {
          size_t n = upload_.size() - p < CHUNK ? upload_.size() - p : CHUNK;
          rtcSendMessage(ws_, (const char *)&upload_[p], (int)n);
        }
        rtcSendMessage(ws_, NetBoardProto::submit_end_frame().c_str(), -1);
        phase_ = UPLOADING;
        continue;
      }
      size_t size = 0;
      if (phase_ == FETCH_WAIT_OK &&
          NetBoardProto::fetch_ok_size(in.text, &size)) {
        // The worker never exceeds the submission cap; a larger claim is
        // a protocol violation, refused before buffering it.
        if (size == 0 || size > 32u * 1024u * 1024u) {
          phase_ = IDLE;
          ev = Event();
          ev.kind = Event::Error;
          ev.reason = "protocol";
          return true;
        }
        download_expect_ = size;
        download_.reserve(size);
        phase_ = FETCHING;
        continue;
      }
      if (phase_ == FETCHING && NetBoardProto::is_fetch_end(in.text)) {
        phase_ = IDLE;
        ev = Event();
        if (download_.size() != download_expect_ || !write_download()) {
          ev.kind = Event::Error;
          ev.reason = download_.size() != download_expect_ ? "protocol"
                                                           : "file";
        } else {
          ev.kind = Event::FetchDone;
        }
        download_.clear();
        return true;
      }
      ev = Event();
      if (NetBoardProto::parse_frame(in.text, ev)) {
        if (ev.kind == Event::Placed || ev.kind == Event::Error) {
          phase_ = IDLE;
          upload_.clear();
        }
        return true;
      }
      // Unknown frame: skip and keep draining.
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!local_errors_.empty()) {
        ev = Event();
        ev.kind = Event::Error;
        ev.reason = local_errors_.front();
        local_errors_.pop_front();
        return true;
      }
    }
    if (closed_flag_.exchange(false)) {
      ev = Event();
      ev.kind = Event::Closed;
      return true;
    }
    return false;
  }

  void close() override {
    if (ws_ >= 0) {
      rtcDeleteWebSocket(ws_);
      ws_ = -1;
    }
    opened_ = false;
    phase_ = IDLE;
    upload_.clear();
    download_.clear();
  }

 private:
  enum Phase { IDLE, UPLOAD_WAIT_OK, UPLOADING, FETCH_WAIT_OK, FETCHING };

  struct Incoming {
    bool binary = false;
    std::string text;
    std::vector<uint8_t> bytes;
  };

  void send_text(const std::string &frame) {
    if (ws_ < 0) return;
    if (opened_.load()) {
      rtcSendMessage(ws_, frame.c_str(), -1);
    } else {
      std::lock_guard<std::mutex> lock(mutex_);
      pending_out_.push_back(frame);
    }
  }

  void local_error(const char *reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    local_errors_.push_back(reason);
  }

  bool write_download() {
    if (fetch_dest_.empty()) return false;
    FILE *fp = fopen(fetch_dest_.c_str(), "wb");
    if (!fp) return false;
    bool ok = download_.empty() ||
              fwrite(&download_[0], 1, download_.size(), fp) ==
                  download_.size();
    if (fclose(fp) != 0) ok = false;
    if (!ok) std::remove(fetch_dest_.c_str());
    return ok;
  }

  static void RTC_API on_open(int, void *p) {
    ((RtcBoard *)p)->opened_ = true;
  }

  static void RTC_API on_message(int, const char *message, int size, void *p) {
    RtcBoard *self = (RtcBoard *)p;
    Incoming in;
    if (size < 0) {
      in.text = message;  // text frame, null-terminated
    } else {
      in.binary = true;
      in.bytes.assign((const uint8_t *)message, (const uint8_t *)message + size);
    }
    std::lock_guard<std::mutex> lock(self->mutex_);
    self->inbox_.push_back(in);
  }

  static void RTC_API on_closed(int, void *p) {
    ((RtcBoard *)p)->closed_flag_ = true;
  }

  static void RTC_API on_error(int, const char *, void *p) {
    ((RtcBoard *)p)->closed_flag_ = true;
  }

  int ws_;
  std::atomic<bool> opened_;
  std::atomic<bool> closed_flag_;
  std::mutex mutex_;
  std::deque<Incoming> inbox_;
  std::deque<std::string> pending_out_;
  std::deque<std::string> local_errors_;
  Phase phase_ = IDLE;
  std::vector<uint8_t> upload_;
  std::vector<uint8_t> download_;
  size_t download_expect_ = 0;
  std::string fetch_dest_;
};

}  // namespace

NetBoard *net_board_create_rtc() { return new RtcBoard(); }

#endif /* NEWTONIA_NET_RTC */
