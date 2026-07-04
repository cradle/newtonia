// Native signaling backend: libdatachannel's built-in WebSocket client
// (TLS via the same MbedTLS as the data channels). Compiled only with
// NEWTONIA_NET_RTC — an empty translation unit everywhere else.
//
// Threading model matches net_transport_rtc.cpp: callbacks fire on the
// library's worker threads and only enqueue raw frames or flip flags;
// poll() parses on the main thread.

#ifdef NEWTONIA_NET_RTC

#include "net_signal.h"

#include <rtc/rtc.h>

#include <atomic>
#include <cstring>
#include <deque>
#include <mutex>

namespace {

class RtcSignal : public NetSignal {
public:
  RtcSignal() : ws_(-1), closed_flag_(false) {}
  ~RtcSignal() { close(); }

  void connect_host(const std::string &url) override {
    open(url + "?role=host");
  }

  void connect_join(const std::string &url, const std::string &code) override {
    std::string upper;
    for (size_t i = 0; i < code.size(); i++) {
      char c = code[i];
      if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
      upper += c;
    }
    open(url + "?role=join&code=" + upper);
  }

  void send_offer(const std::string &sdp) override {
    send_frame("{\"t\":\"offer\",\"sdp\":\"" + NetSig::json_escape(sdp) + "\"}");
  }

  void send_answer(const std::string &sdp) override {
    send_frame("{\"t\":\"answer\",\"sdp\":\"" + NetSig::json_escape(sdp) + "\"}");
  }

  bool poll(Event &ev) override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      while (!inbox_.empty()) {
        std::string frame = inbox_.front();
        inbox_.pop_front();
        if (NetSig::parse_frame(frame, ev)) return true;
        // Unknown frame: skip and keep draining.
      }
    }
    if (closed_flag_.exchange(false)) {
      ev.kind = Event::Closed;
      ev.text.clear();
      return true;
    }
    return false;
  }

  void close() override {
    if (ws_ >= 0) {
      rtcDeleteWebSocket(ws_);
      ws_ = -1;
    }
  }

private:
  void open(const std::string &full_url) {
    close();
    ws_ = rtcCreateWebSocket(full_url.c_str());
    if (ws_ < 0) {
      closed_flag_ = true;
      return;
    }
    rtcSetUserPointer(ws_, this);
    rtcSetMessageCallback(ws_, &RtcSignal::on_message);
    rtcSetClosedCallback(ws_, &RtcSignal::on_closed);
    rtcSetErrorCallback(ws_, &RtcSignal::on_error);
  }

  void send_frame(const std::string &json) {
    if (ws_ >= 0) rtcSendMessage(ws_, json.c_str(), -1);  // -1 = text frame
  }

  static void RTC_API on_message(int, const char *message, int size, void *p) {
    RtcSignal *self = (RtcSignal *)p;
    // Text frames arrive with a negative size (null-terminated).
    std::string frame = size >= 0 ? std::string(message, (size_t)size)
                                  : std::string(message);
    std::lock_guard<std::mutex> lock(self->mutex_);
    self->inbox_.push_back(frame);
  }

  static void RTC_API on_closed(int, void *p) {
    ((RtcSignal *)p)->closed_flag_ = true;
  }

  static void RTC_API on_error(int, const char *, void *p) {
    ((RtcSignal *)p)->closed_flag_ = true;
  }

  int ws_;
  std::atomic<bool> closed_flag_;
  std::mutex mutex_;
  std::deque<std::string> inbox_;
};

}  // namespace

NetSignal *net_signal_create_rtc() { return new RtcSignal(); }

#endif /* NEWTONIA_NET_RTC */
