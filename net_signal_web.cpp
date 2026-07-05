// Web signaling backend: the browser's native WebSocket via EM_JS glue,
// mirroring the Module.__nwnet pattern of net_transport_web.cpp. Compiled
// only under __EMSCRIPTEN__ — an empty translation unit everywhere else.

#ifdef __EMSCRIPTEN__

#include "net_signal.h"

#include <emscripten.h>

#include <stdlib.h>

EM_JS_DEPS(nwsig_deps, "$UTF8ToString,$stringToUTF8,$lengthBytesUTF8");

// Signal-server override for the web build (no env vars in a browser):
// Module.NWTN_SIGNAL_URL, or ?signal=... in the page URL. Returns a
// malloc'd string ("" = use the baked default); caller frees.
EM_JS(char *, nwsig_url_override, (), {
  var url = "";
  try {
    if (Module.NWTN_SIGNAL_URL) url = Module.NWTN_SIGNAL_URL;
    else {
      var q = new URLSearchParams(location.search).get('signal');
      if (q) url = q;
    }
  } catch (e) {}
  var n = lengthBytesUTF8(url) + 1;
  var p = _malloc(n);
  stringToUTF8(url, p, n);
  return p;
});

std::string net_signal_url_web_override() {
  char *p = nwsig_url_override();
  std::string url(p);
  free(p);
  return url;
}

EM_JS(int, nwsig_open, (const char *url), {
  var S = Module.__nwsig || (Module.__nwsig = { next: 1, conns: {} });
  var h = S.next++;
  var c = { ws: null, inbox: [], closed: false };
  S.conns[h] = c;
  try {
    var ws = new WebSocket(UTF8ToString(url));
    c.ws = ws;
    ws.onmessage = function(m) { if (typeof m.data === 'string') c.inbox.push(m.data); };
    ws.onclose = function() { c.closed = true; };
    ws.onerror = function() { c.closed = true; };
  } catch (e) {
    c.closed = true;
  }
  return h;
});

EM_JS(void, nwsig_send, (int h, const char *json), {
  var S = Module.__nwsig; if (!S) return;
  var c = S.conns[h]; if (!c || !c.ws) return;
  if (c.ws.readyState === 0) {
    // Still connecting: queue through onopen so early sends aren't lost.
    var text = UTF8ToString(json);
    var prev = c.ws.onopen;
    c.ws.onopen = function() { if (prev) prev(); try { c.ws.send(text); } catch (e) {} };
  } else if (c.ws.readyState === 1) {
    try { c.ws.send(UTF8ToString(json)); } catch (e) {}
  }
});

// Returns a malloc'd UTF-8 frame (caller frees) or 0 when none pending.
EM_JS(char *, nwsig_poll_frame, (int h), {
  var S = Module.__nwsig; if (!S) return 0;
  var c = S.conns[h]; if (!c || !c.inbox.length) return 0;
  var text = c.inbox.shift();
  var n = lengthBytesUTF8(text) + 1;
  var p = _malloc(n);
  stringToUTF8(text, p, n);
  return p;
});

EM_JS(int, nwsig_closed, (int h), {
  var S = Module.__nwsig; if (!S) return 1;
  var c = S.conns[h];
  return (!c || c.closed) ? 1 : 0;
});

EM_JS(void, nwsig_close, (int h), {
  var S = Module.__nwsig; if (!S) return;
  var c = S.conns[h]; if (!c) return;
  try { if (c.ws) c.ws.close(); } catch (e) {}
  delete S.conns[h];
});

namespace {

class WebSignal : public NetSignal {
public:
  WebSignal() : handle_(0), closed_reported_(false) {}
  ~WebSignal() { close(); }

  void connect_host(const std::string &url) override {
    open(url + "?role=host");
  }

  void connect_host_reclaim(const std::string &url, const std::string &code,
                            const std::string &token) override {
    open(url + "?role=host&code=" + code + "&token=" + token);
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

  void send_cand(const std::string &mid, const std::string &cand) override {
    send_frame("{\"t\":\"cand\",\"mid\":\"" + NetSig::json_escape(mid) +
               "\",\"cand\":\"" + NetSig::json_escape(cand) + "\"}");
  }

  bool poll(Event &ev) override {
    if (!handle_) return false;
    char *frame;
    while ((frame = nwsig_poll_frame(handle_)) != nullptr) {
      std::string text(frame);
      free(frame);
      if (NetSig::parse_frame(text, ev)) return true;
    }
    if (!closed_reported_ && nwsig_closed(handle_)) {
      closed_reported_ = true;
      ev.kind = Event::Closed;
      ev.text.clear();
      return true;
    }
    return false;
  }

  void close() override {
    if (handle_) {
      nwsig_close(handle_);
      handle_ = 0;
    }
  }

private:
  void open(const std::string &full_url) {
    close();
    closed_reported_ = false;
    handle_ = nwsig_open(full_url.c_str());
  }

  void send_frame(const std::string &json) {
    if (handle_) nwsig_send(handle_, json.c_str());
  }

  int handle_;
  bool closed_reported_;
};

}  // namespace

NetSignal *net_signal_create_web() { return new WebSignal(); }

#endif /* __EMSCRIPTEN__ */
