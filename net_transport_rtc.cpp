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
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>

namespace {

const char *STUN_SERVER = "stun:stun.l.google.com:19302";

class RtcTransport : public NetTransport {
public:
  void set_ice_servers(const std::vector<std::string> &servers) override {
    // Each entry is "urls\nusername\ncredential" (Cloudflare Calls TURN via
    // the signal relay). libdatachannel takes credentials embedded in the
    // URL: turn:USER:PASS@host:port?transport=udp
    ice_extra_.clear();
    for (size_t i = 0; i < servers.size(); i++) {
      const std::string &s = servers[i];
      size_t a = s.find('\n');
      if (a == std::string::npos) { ice_extra_.push_back(s); continue; }
      size_t b = s.find('\n', a + 1);
      std::string urls = s.substr(0, a);
      std::string user = s.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
      std::string cred = b == std::string::npos ? "" : s.substr(b + 1);
      size_t scheme = urls.find(':');
      if (scheme == std::string::npos || user.empty()) {
        ice_extra_.push_back(urls);
      } else {
        ice_extra_.push_back(urls.substr(0, scheme + 1) + user + ":" + cred +
                             "@" + urls.substr(scheme + 1));
      }
    }
  }

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

  void set_force_relay(bool on) override { relay_only_ = on; }

  // ---- trickle ICE (M3-2b) ----
  void set_trickle(bool on) override { trickle_ = on; }

  bool poll_local_candidate(std::string &out) override {
    std::lock_guard<std::mutex> lock(cand_mutex_);
    if (cand_out_.empty()) return false;
    out = cand_out_.front();
    cand_out_.pop_front();
    return true;
  }

  void add_remote_candidate(const std::string &mid,
                            const std::string &cand) override {
    if (pc_ < 0) return;
    rtcAddRemoteCandidate(pc_, cand.c_str(), mid.c_str());
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
    // Test hook: NEWTONIA_NET_TEST_RX_DELAY_MS holds every received
    // message for N ms — RTT-realistic staleness on a loopback pair
    // (the container has no netem; TESTING.md's iptables rig covers
    // loss, this covers latency).
    static int rx_delay = -1;
    if (rx_delay < 0) {
      const char *e = std::getenv("NEWTONIA_NET_TEST_RX_DELAY_MS");
      rx_delay = (e && e[0]) ? std::atoi(e) : 0;
    }
    if (rx_delay > 0) {
      uint64_t now = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count();
      if (inbox_t_.front() + (uint64_t)rx_delay > now) return false;
    }
    out.swap(inbox_.front());
    inbox_.pop_front();
    if (!inbox_t_.empty()) inbox_t_.pop_front();
    return true;
  }

  std::string connection_info() const override {
    if (pc_ < 0 || !connected()) return std::string();
    char local[512], remote[512];
    if (rtcGetSelectedCandidatePair(pc_, local, (int)sizeof(local), remote,
                                    (int)sizeof(remote)) < 0)
      return std::string();
    return cand_type(local) + "/" + cand_type(remote);
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
  // The token after " typ " in a candidate line (host/srflx/prflx/relay).
  static std::string cand_type(const char *cand) {
    const char *t = std::strstr(cand, " typ ");
    if (!t) return "?";
    t += 5;
    const char *e = t;
    while (*e && *e != ' ') e++;
    return std::string(t, (size_t)(e - t));
  }

  // NEWTONIA_NET_FORCE_RELAY=1: relay-only ICE for TURN testing — a
  // loopback/LAN pair would otherwise always pick the direct path and
  // the relay (and its credential lifetime) would go unexercised.
  static bool force_relay() {
    static int on = -1;
    if (on < 0) {
      const char *e = std::getenv("NEWTONIA_NET_FORCE_RELAY");
      on = (e && e[0] && e[0] != '0') ? 1 : 0;
    }
    return on != 0;
  }

  void open_peer() {
    // SCTP tuning for sparse real-time game traffic (global; applies to
    // peer connections created after it). Every DataChannel — the
    // "unreliable" one included — rides ONE SCTP association, and with
    // only ~10 small messages a second in flight a lost packet rarely
    // gathers the 3 duplicate SACKs fast-retransmit needs: recovery
    // waits for the retransmission timeout, and the stock ~1 s RTO froze
    // the whole association (measured: 0.6-1.7 s input blackouts on a
    // relay path whose ICMP ping was clean). Tighter RTO bounds cut such
    // stalls to ~200-500 ms; the 10 ms SACK delay speeds the peer's loss
    // reports the same way. Browsers expose no equivalent, so web
    // clients keep the stock behaviour.
    static bool sctp_tuned = false;
    if (!sctp_tuned) {
      sctp_tuned = true;
      rtcSctpSettings s;
      std::memset(&s, 0, sizeof(s));  // 0 = library default elsewhere
      s.minRetransmitTimeoutMs = 200;
      s.initialRetransmitTimeoutMs = 500;
      s.maxRetransmitTimeoutMs = 1500;
      s.delayedSackTimeMs = 10;
      // The RTO cap has a side effect: the association's retransmit
      // budget (stock ~10 attempts) burns through in ~12 s of continuous
      // loss instead of the stock ~minute, killing the session into the
      // pause/rejoin cycle on outages it used to just freeze through.
      // More attempts restore long-outage tolerance (~45 s) while keeping
      // the fast per-packet recovery the tight RTOs buy.
      s.maxRetransmitAttempts = 30;
      rtcSetSctpSettings(&s);
    }

    rtcConfiguration config;
    std::memset(&config, 0, sizeof(config));
    ice_ptrs_.clear();
    ice_ptrs_.push_back(STUN_SERVER);
    for (size_t i = 0; i < ice_extra_.size(); i++)
      ice_ptrs_.push_back(ice_extra_[i].c_str());
    config.iceServers = &ice_ptrs_[0];
    config.iceServersCount = (int)ice_ptrs_.size();
    if (force_relay() || relay_only_)
      config.iceTransportPolicy = RTC_TRANSPORT_POLICY_RELAY;

    pc_ = rtcCreatePeerConnection(&config);
    if (pc_ < 0) {
      failed_ = true;
      return;
    }
    rtcSetUserPointer(pc_, this);
    rtcSetStateChangeCallback(pc_, &RtcTransport::on_state_change);
    rtcSetGatheringStateChangeCallback(pc_, &RtcTransport::on_gathering_state);
    rtcSetDataChannelCallback(pc_, &RtcTransport::on_data_channel);
    if (trickle_) {
      // Trickle (M3-2b): the SDP is usable the moment it exists — no
      // gathering wait; candidates stream separately via the signal relay.
      rtcSetLocalDescriptionCallback(pc_, &RtcTransport::on_local_description);
      rtcSetLocalCandidateCallback(pc_, &RtcTransport::on_local_candidate);
    }
  }

  static void RTC_API on_local_description(int, const char *sdp, const char *,
                                           void *ptr) {
    RtcTransport *self = (RtcTransport *)ptr;
    if (!sdp) return;
    std::lock_guard<std::mutex> lock(self->desc_mutex_);
    self->local_desc_.assign(sdp);
    self->desc_ready_ = true;
  }

  static void RTC_API on_local_candidate(int, const char *cand,
                                         const char *mid, void *ptr) {
    RtcTransport *self = (RtcTransport *)ptr;
    if (!cand || !cand[0]) return;  // end-of-candidates marker
    std::lock_guard<std::mutex> lock(self->cand_mutex_);
    self->cand_out_.push_back(std::string(mid ? mid : "0") + "\n" + cand);
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
    self->inbox_t_.push_back((uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
  }

  int pc_;
  int dc_rel_, dc_unrel_;
  std::vector<std::string> ice_extra_;   // composed turn: URLs
  std::vector<const char *> ice_ptrs_;
  bool relay_only_ = false;              // set_force_relay (lobby test hook)

  std::atomic<bool> rel_open_, unrel_open_;
  mutable std::atomic<bool> desc_ready_;
  bool trickle_ = false;
  std::mutex cand_mutex_;
  std::deque<std::string> cand_out_;  // gathered local candidates, "mid\ncand"
  std::atomic<bool> failed_;
  bool closed_;
  bool gather_started_ = false;
  std::chrono::steady_clock::time_point gather_start_;

  mutable std::mutex desc_mutex_;
  mutable std::string local_desc_;

  std::mutex inbox_mutex_;
  std::deque<std::vector<unsigned char> > inbox_;
  std::deque<uint64_t> inbox_t_;  // arrival stamps (RX-delay test hook)
};

}  // namespace

NetTransport *create_rtc_transport() {
  return new RtcTransport();
}

#endif /* NEWTONIA_NET_RTC */
