// Native netplay backend over the libdatachannel C API (rtc/rtc.h).
// The whole file compiles away unless NEWTONIA_NET_RTC is defined, which
// only xbox/CMakeLists.txt does (NEWTONIA_NET=ON) — every other build glob
// picks this file up as an empty translation unit.
//
// Threading model: libdatachannel fires all callbacks on its own worker
// threads. Callbacks only enqueue into a mutex-guarded deque or flip
// atomic flags; the game consumes everything from the main thread via
// poll()/the const accessors. No rtc call ever blocks the game loop.
//
// Signaling is non-trickle: local_description_ready() only flips once ICE
// gathering completes, so local_description() returns the full SDP with
// every candidate embedded — ready for the clipboard copy-paste flow.

#ifdef NEWTONIA_NET_RTC

#include "net_transport.h"

#include <rtc/rtc.h>

#include <chrono>
#include <atomic>
#include <cstring>
#include <deque>
#include <mutex>

namespace {

const char *STUN_SERVER = "stun:stun.l.google.com:19302";

class RtcTransport : public NetTransport {
public:
  RtcTransport()
      : pc_(-1), dc_rel_(-1), dc_unrel_(-1),
        rel_open_(false), unrel_open_(false),
        desc_ready_(false), failed_(false), closed_(false) {}

  ~RtcTransport() { close(); }

  void start_host() override {
    open_peer();
    if (pc_ < 0) return;

    // Creating the first channel kicks off negotiation; the second one is
    // negotiated in-band over the same SCTP association.
    dc_rel_ = rtcCreateDataChannel(pc_, "rel");

    rtcDataChannelInit init;
    std::memset(&init, 0, sizeof(init));
    init.reliability.unordered = true;
    init.reliability.unreliable = true;
    init.reliability.maxRetransmits = 0;
    dc_unrel_ = rtcCreateDataChannelEx(pc_, "unrel", &init);

    if (dc_rel_ < 0 || dc_unrel_ < 0) {
      failed_ = true;
      return;
    }
    adopt_channel(dc_rel_, true);
    adopt_channel(dc_unrel_, false);
    gather_start_ = std::chrono::steady_clock::now();
    gather_started_ = true;
  }

  void start_join(const std::string &remote_offer) override {
    open_peer();
    if (pc_ < 0) return;
    // The answer is generated automatically; channels arrive through
    // on_data_channel once the connection establishes.
    if (rtcSetRemoteDescription(pc_, remote_offer.c_str(), "offer") < 0) {
      failed_ = true;
      return;
    }
    gather_start_ = std::chrono::steady_clock::now();
    gather_started_ = true;
  }

  bool local_description_ready() const override {
    if (desc_ready_) return true;
    // Mirror of the web backend's descTimeoutMs fallback: slow or filtered
    // STUN (e.g. a Windows firewall quietly eating UDP) can stall gathering
    // for tens of seconds before it reports complete. After 8 s, snapshot
    // the SDP with whatever candidates exist — host candidates gather
    // instantly and are enough for LAN/same-machine play; the completed
    // description still replaces this one if gathering finishes later.
    if (gather_started_ && pc_ >= 0 &&
        std::chrono::steady_clock::now() - gather_start_ >
            std::chrono::seconds(8)) {
      char buffer[8192];
      int size = rtcGetLocalDescription(pc_, buffer, (int)sizeof(buffer));
      if (size > 0) {
        std::lock_guard<std::mutex> lock(desc_mutex_);
        local_desc_.assign(buffer);
        desc_ready_ = true;
      }
    }
    return desc_ready_;
  }

  std::string local_description() const override {
    std::lock_guard<std::mutex> lock(desc_mutex_);
    return local_desc_;
  }

  void set_remote_answer(const std::string &remote_answer) override {
    if (pc_ < 0) return;
    if (rtcSetRemoteDescription(pc_, remote_answer.c_str(), "answer") < 0)
      failed_ = true;
  }

  bool connected() const override { return rel_open_ && unrel_open_; }
  bool failed() const override { return failed_; }

  void send_reliable(const void *data, size_t size) override {
    if (rel_open_)
      rtcSendMessage(dc_rel_, (const char *)data, (int)size);
  }

  void send_unreliable(const void *data, size_t size) override {
    if (unrel_open_)
      rtcSendMessage(dc_unrel_, (const char *)data, (int)size);
  }

  bool poll(std::vector<unsigned char> &out) override {
    std::lock_guard<std::mutex> lock(inbox_mutex_);
    if (inbox_.empty()) return false;
    out.swap(inbox_.front());
    inbox_.pop_front();
    return true;
  }

  void close() override {
    if (closed_) return;
    closed_ = true;
    if (dc_rel_ >= 0) { rtcDeleteDataChannel(dc_rel_); dc_rel_ = -1; }
    if (dc_unrel_ >= 0) { rtcDeleteDataChannel(dc_unrel_); dc_unrel_ = -1; }
    if (pc_ >= 0) { rtcDeletePeerConnection(pc_); pc_ = -1; }
    rel_open_ = false;
    unrel_open_ = false;
  }

private:
  void open_peer() {
    rtcConfiguration config;
    std::memset(&config, 0, sizeof(config));
    config.iceServers = &STUN_SERVER;
    config.iceServersCount = 1;

    pc_ = rtcCreatePeerConnection(&config);
    if (pc_ < 0) {
      failed_ = true;
      return;
    }
    rtcSetUserPointer(pc_, this);
    rtcSetStateChangeCallback(pc_, &RtcTransport::on_state_change);
    rtcSetGatheringStateChangeCallback(pc_, &RtcTransport::on_gathering_state);
    rtcSetDataChannelCallback(pc_, &RtcTransport::on_data_channel);
  }

  // Wire a channel (created locally or received remotely) into this
  // transport. Channel ids are stored before callbacks are attached, so
  // the open callback can tell rel from unrel by id.
  void adopt_channel(int dc, bool reliable) {
    if (reliable) dc_rel_ = dc; else dc_unrel_ = dc;
    rtcSetUserPointer(dc, this);
    rtcSetOpenCallback(dc, &RtcTransport::on_channel_open);
    rtcSetClosedCallback(dc, &RtcTransport::on_channel_closed);
    rtcSetMessageCallback(dc, &RtcTransport::on_message);
    // The open event may have fired before the callback was attached (the
    // join side adopts channels from on_data_channel, which can lose that
    // race) — rtcSetOpenCallback does not retro-fire, so check directly.
    if (rtcIsOpen(dc)) {
      if (reliable) rel_open_ = true; else unrel_open_ = true;
    }
  }

  // --- libdatachannel worker-thread callbacks: enqueue/flag only --------

  static void RTC_API on_state_change(int, rtcState state, void *ptr) {
    RtcTransport *self = (RtcTransport *)ptr;
    // DISCONNECTED is deliberately not fatal: it is transient per the
    // WebRTC spec and fires spuriously during slow ICE (e.g. while STUN
    // requests are still timing out). True losses surface as FAILED, a
    // channel close, or the Phase 8 dead-man switch.
    if (state == RTC_FAILED || state == RTC_CLOSED)
      self->failed_ = true;
  }

  static void RTC_API on_gathering_state(int pc, rtcGatheringState state,
                                         void *ptr) {
    RtcTransport *self = (RtcTransport *)ptr;
    if (state != RTC_GATHERING_COMPLETE) return;
    char buffer[8192];
    int size = rtcGetLocalDescription(pc, buffer, (int)sizeof(buffer));
    if (size < 0) {
      self->failed_ = true;
      return;
    }
    {
      std::lock_guard<std::mutex> lock(self->desc_mutex_);
      self->local_desc_.assign(buffer);
    }
    self->desc_ready_ = true;
  }

  static void RTC_API on_data_channel(int, int dc, void *ptr) {
    RtcTransport *self = (RtcTransport *)ptr;
    char label[32] = {0};
    if (rtcGetDataChannelLabel(dc, label, (int)sizeof(label)) < 0) return;
    self->adopt_channel(dc, std::strcmp(label, "rel") == 0);
  }

  static void RTC_API on_channel_open(int dc, void *ptr) {
    RtcTransport *self = (RtcTransport *)ptr;
    if (dc == self->dc_rel_) self->rel_open_ = true;
    if (dc == self->dc_unrel_) self->unrel_open_ = true;
  }

  static void RTC_API on_channel_closed(int, void *ptr) {
    RtcTransport *self = (RtcTransport *)ptr;
    // Either channel closing mid-session means the peer is gone.
    if (self->rel_open_ || self->unrel_open_) self->failed_ = true;
  }

  static void RTC_API on_message(int, const char *message, int size,
                                 void *ptr) {
    RtcTransport *self = (RtcTransport *)ptr;
    // Negative size flags a string message; the protocol is binary-only,
    // but store it faithfully rather than dropping it.
    size_t n = size >= 0 ? (size_t)size : std::strlen(message);
    std::vector<unsigned char> bytes((const unsigned char *)message,
                                     (const unsigned char *)message + n);
    std::lock_guard<std::mutex> lock(self->inbox_mutex_);
    self->inbox_.push_back(std::vector<unsigned char>());
    self->inbox_.back().swap(bytes);
  }

  int pc_;
  int dc_rel_, dc_unrel_;

  std::atomic<bool> rel_open_, unrel_open_;
  mutable std::atomic<bool> desc_ready_;
  std::atomic<bool> failed_;
  bool closed_;
  bool gather_started_ = false;
  std::chrono::steady_clock::time_point gather_start_;

  mutable std::mutex desc_mutex_;
  mutable std::string local_desc_;

  std::mutex inbox_mutex_;
  std::deque<std::vector<unsigned char> > inbox_;
};

}  // namespace

NetTransport *create_rtc_transport() {
  return new RtcTransport();
}

#endif /* NEWTONIA_NET_RTC */
