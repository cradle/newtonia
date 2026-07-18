// Browser netplay backend: RTCPeerConnection via EM_JS glue on a
// Module.__nwnet object, polled from the main thread (same poll-style
// pattern as web_on_idb_ready in web_main.cpp). Compiled only under
// Emscripten; every other build sees an empty translation unit.
//
// All state and logic live in JS (Module.__nwnet, installed once by
// nw_init) keyed by integer handles, so multiple transports coexist just
// like the native backend, and the glue can be driven straight from the
// browser console / tests. The C++ side is thin EM_JS wrappers plus the
// WebTransport class mapping the NetTransport interface onto them.
//
// The browser is single-threaded: WebRTC events land on the same event
// loop the game runs on, so unlike the native backend no locking is
// needed — "callbacks" here are JS promise resolutions that only touch
// the __nwnet state, which C++ reads via the poll-style wrappers between
// frames.

#ifdef __EMSCRIPTEN__

#include "net_transport.h"
#include "net_signal.h"

#include <emscripten.h>

#include <vector>

EM_JS_DEPS(nwnet_deps, "$UTF8ToString,$stringToUTF8,$lengthBytesUTF8");

// clang-format off
EM_JS(void, nw_init, (), {
  if (Module.__nwnet) return;
  var N = {
    conns: {},
    next: 1,
    clipPending: false,
    clipText: null,

    create: function() {
      var h = N.next++;
      N.conns[h] = { pc: null, rel: null, unrel: null, inbox: [],
                     localDesc: null, failed: false,
                     relOpen: false, unrelOpen: false,
                     trickle: false, cands: [], relayOnly: false };
      return h;
    },

    _chan: function(c, ch, isRel) {
      ch.binaryType = 'arraybuffer';
      ch.onopen = function() {
        if (isRel) c.relOpen = true; else c.unrelOpen = true;
      };
      ch.onclose = function() {
        // A channel closing mid-session means the peer is gone.
        if (c.relOpen || c.unrelOpen) c.failed = true;
      };
      ch.onmessage = function(ev) {
        c.inbox.push(new Uint8Array(ev.data));
      };
      if (isRel) c.rel = ch; else c.unrel = ch;
    },

    _pc: function(c) {
      var servers = [{ urls: 'stun:stun.l.google.com:19302' }];
      if (Module.__nwice) servers = servers.concat(Module.__nwice);
      var pc = new RTCPeerConnection({
        iceServers: servers,
        iceTransportPolicy: c.relayOnly ? 'relay' : 'all'
      });
      pc.onicegatheringstatechange = function() {
        // Non-trickle: expose the SDP once every candidate is in it.
        if (!c.trickle && pc.iceGatheringState === 'complete' &&
            pc.localDescription && c.localDesc === null)
          c.localDesc = pc.localDescription.sdp;
      };
      pc.onicecandidate = function(ev) {
        // Trickle (M3-2b): stream each candidate to C++ as gathered.
        if (c.trickle && ev.candidate && ev.candidate.candidate)
          c.cands.push((ev.candidate.sdpMid || '0') + '\n' +
                       ev.candidate.candidate);
      };
      pc.onconnectionstatechange = function() {
        // Diagnostics cache: sample the selected candidate pair's types
        // ("host/relay"...) once connected; refreshed every 2 s.
        if (pc.connectionState === 'connected' && !c.pathTimer) {
          c.pathTimer = setInterval(function() {
            pc.getStats().then(function(stats) {
              var pair = null, byId = {};
              stats.forEach(function(r) { byId[r.id] = r; });
              stats.forEach(function(r) {
                if (r.type === 'transport' && r.selectedCandidatePairId)
                  pair = byId[r.selectedCandidatePairId];
                if (!pair && r.type === 'candidate-pair' && r.nominated &&
                    r.state === 'succeeded')
                  pair = r;
              });
              if (!pair) return;
              var l = byId[pair.localCandidateId];
              var r2 = byId[pair.remoteCandidateId];
              var typ = function(c2) {
                if (!c2) return '?';
                var t = c2.candidateType;
                return t === 'srflx' || t === 'prflx' || t === 'relay' ||
                       t === 'host' ? t : String(t);
              };
              c.pathInfo = typ(l) + '/' + typ(r2);
            }).catch(function() {});
          }, 2000);
        }
        // 'disconnected' is transient per spec and can recover; only
        // 'failed'/'closed' are fatal (plus channel onclose below).
        var s = pc.connectionState;
        if (s === 'failed' || s === 'closed')
          c.failed = true;
      };
      pc.ondatachannel = function(ev) {
        N._chan(c, ev.channel, ev.channel.label === 'rel');
      };
      c.pc = pc;
      return pc;
    },

    // Gathering can stall for a long time (slow/unreachable STUN, odd
    // interfaces) and browsers may then never report 'complete'. The SDP
    // snapshot already embeds every candidate gathered so far, so after
    // this grace period take what we have rather than hanging the lobby.
    descTimeoutMs: 8000,
    _armDescTimeout: function(c) {
      setTimeout(function() {
        if (c.localDesc === null && c.pc && c.pc.localDescription)
          c.localDesc = c.pc.localDescription.sdp;
      }, N.descTimeoutMs);
    },

    startHost: function(h) {
      var c = N.conns[h]; if (!c) return;
      var pc = N._pc(c);
      N._chan(c, pc.createDataChannel('rel'), true);
      N._chan(c, pc.createDataChannel('unrel',
                                      { ordered: false, maxRetransmits: 0 }),
              false);
      pc.createOffer()
        .then(function(o) { return pc.setLocalDescription(o); })
        .then(function() {
          // Trickle: the SDP is usable immediately, candidates follow.
          if (c.trickle) c.localDesc = pc.localDescription.sdp;
          else N._armDescTimeout(c);
        })
        .catch(function() { c.failed = true; });
    },

    startJoin: function(h, offerSdp) {
      var c = N.conns[h]; if (!c) return;
      var pc = N._pc(c);
      pc.setRemoteDescription({ type: 'offer', sdp: offerSdp })
        .then(function() { return pc.createAnswer(); })
        .then(function(a) { return pc.setLocalDescription(a); })
        .then(function() {
          if (c.trickle) c.localDesc = pc.localDescription.sdp;
          else N._armDescTimeout(c);
        })
        .catch(function() { c.failed = true; });
    },

    addCand: function(h, mid, cand) {
      var c = N.conns[h]; if (!c || !c.pc) return;
      c.pc.addIceCandidate({ sdpMid: mid, candidate: cand })
        .catch(function() {});  // late/duplicate candidates are harmless
    },

    setAnswer: function(h, sdp) {
      var c = N.conns[h]; if (!c || !c.pc) return;
      c.pc.setRemoteDescription({ type: 'answer', sdp: sdp })
        .catch(function() { c.failed = true; });
    },

    send: function(h, rel, bytes) {
      var c = N.conns[h]; if (!c) return;
      var ch = rel ? c.rel : c.unrel;
      if (ch && ch.readyState === 'open') {
        try { ch.send(bytes); } catch (e) {}
      }
    },

    close: function(h) {
      var c = N.conns[h]; if (!c) return;
      // Clear the open flags first so the onclose handlers don't read a
      // deliberate shutdown as a peer failure.
      c.relOpen = false; c.unrelOpen = false;
      if (c.pathTimer) { clearInterval(c.pathTimer); c.pathTimer = null; }
      try { if (c.rel) c.rel.close(); } catch (e) {}
      try { if (c.unrel) c.unrel.close(); } catch (e) {}
      try { if (c.pc) c.pc.close(); } catch (e) {}
      delete N.conns[h];
    },

    clipWrite: function(text) {
      if (navigator.clipboard && navigator.clipboard.writeText)
        navigator.clipboard.writeText(text).catch(function() {});
    },

    clipReadStart: function() {
      N.clipText = null;
      if (navigator.clipboard && navigator.clipboard.readText) {
        N.clipPending = true;
        navigator.clipboard.readText().then(
            function(t) { N.clipText = t; N.clipPending = false; },
            function() { N.clipText = null; N.clipPending = false; });
      } else {
        N.clipPending = false;
      }
    }
  };
  Module.__nwnet = N;
});

EM_JS(int, nw_create, (), { return Module.__nwnet.create(); });
EM_JS(void, nw_start_host, (int h), { Module.__nwnet.startHost(h); });
EM_JS(void, nw_start_join, (int h, const char *offer),
      { Module.__nwnet.startJoin(h, UTF8ToString(offer)); });
EM_JS(int, nw_desc_ready, (int h), {
  var c = Module.__nwnet.conns[h];
  return (c && c.localDesc !== null) ? 1 : 0;
});
EM_JS(int, nw_desc_len, (int h), {
  var c = Module.__nwnet.conns[h];
  return (c && c.localDesc !== null) ? lengthBytesUTF8(c.localDesc) : 0;
});
EM_JS(void, nw_desc_copy, (int h, char *buf, int len), {
  var c = Module.__nwnet.conns[h];
  if (c && c.localDesc !== null) stringToUTF8(c.localDesc, buf, len);
});
EM_JS(void, nw_set_answer, (int h, const char *sdp),
      { Module.__nwnet.setAnswer(h, UTF8ToString(sdp)); });
EM_JS(int, nw_connected, (int h), {
  var c = Module.__nwnet.conns[h];
  return (c && c.relOpen && c.unrelOpen) ? 1 : 0;
});
EM_JS(int, nw_failed, (int h), {
  var c = Module.__nwnet.conns[h];
  return (c && c.failed) ? 1 : 0;
});
EM_JS(int, nw_buffered, (int h), {
  var c = Module.__nwnet.conns[h];
  if (!c) return 0;
  return ((c.rel && c.rel.bufferedAmount) | 0) +
         ((c.unrel && c.unrel.bufferedAmount) | 0);
});
EM_JS(void, nw_send, (int h, int rel, const void *data, int size), {
  // slice() copies out of the wasm heap so the channel never holds a view
  // over memory the game is about to reuse.
  Module.__nwnet.send(h, rel, HEAPU8.slice(data, data + size));
});
EM_JS(int, nw_poll_size, (int h), {
  var c = Module.__nwnet.conns[h];
  return (c && c.inbox.length) ? c.inbox[0].length : -1;
});
EM_JS(void, nw_poll_take, (int h, void *buf), {
  var c = Module.__nwnet.conns[h];
  if (c && c.inbox.length) HEAPU8.set(c.inbox.shift(), buf);
});
EM_JS(void, nw_close, (int h), { Module.__nwnet.close(h); });
EM_JS(void, nw_path_info, (int h, char *buf, int len), {
  var c = Module.__nwnet.conns[h];
  stringToUTF8(c && c.pathInfo ? c.pathInfo : "", buf, len);
});

EM_JS(void, nw_set_force_relay, (int h, int on), {
  var c = Module.__nwnet.conns[h];
  if (c) c.relayOnly = !!on;
});

// ---- trickle ICE (M3-2b) --------------------------------------------------
EM_JS(void, nw_set_trickle, (int h, int on), {
  var c = Module.__nwnet.conns[h];
  if (c) c.trickle = !!on;
});
EM_JS(int, nw_cand_len, (int h), {
  var c = Module.__nwnet.conns[h];
  return (c && c.cands.length) ? lengthBytesUTF8(c.cands[0]) : -1;
});
EM_JS(void, nw_cand_take, (int h, char *buf, int len), {
  var c = Module.__nwnet.conns[h];
  if (c && c.cands.length) stringToUTF8(c.cands.shift(), buf, len);
});
EM_JS(void, nw_add_cand, (int h, const char *mid, const char *cand), {
  Module.__nwnet.addCand(h, UTF8ToString(mid), UTF8ToString(cand));
});

EM_JS(void, nw_clip_write, (const char *text),
      { Module.__nwnet.clipWrite(UTF8ToString(text)); });
EM_JS(void, nw_clip_read_start, (), { Module.__nwnet.clipReadStart(); });
EM_JS(int, nw_clip_pending, (), {
  return Module.__nwnet.clipPending ? 1 : 0;
});
EM_JS(int, nw_clip_len, (), {
  var t = Module.__nwnet.clipText;
  return t === null ? -1 : lengthBytesUTF8(t);
});
EM_JS(void, nw_clip_copy, (char *buf, int len), {
  var t = Module.__nwnet.clipText;
  if (t !== null) stringToUTF8(t, buf, len);
  Module.__nwnet.clipText = null;
});
// clang-format on

EM_JS(void, nwnet_set_ice, (const char *json), {
  try { Module.__nwice = JSON.parse(UTF8ToString(json)); } catch (e) {}
});

class WebTransport : public NetTransport {
public:
  // Triples "urls\nuser\ncred" -> RTCIceServer objects, applied to every
  // peer connection created after this call (Module.__nwice).
  void set_ice_servers(const std::vector<std::string> &servers) override {
    std::string json = "[";
    for (size_t i = 0; i < servers.size(); i++) {
      const std::string &s = servers[i];
      size_t a = s.find('\n');
      size_t b = a == std::string::npos ? a : s.find('\n', a + 1);
      std::string urls = s.substr(0, a);
      std::string user = a == std::string::npos ? "" :
          s.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
      std::string cred = b == std::string::npos ? "" : s.substr(b + 1);
      if (i) json += ",";
      json += "{\"urls\":\"" + NetSig::json_escape(urls) + "\"";
      if (!user.empty())
        json += ",\"username\":\"" + NetSig::json_escape(user) +
                "\",\"credential\":\"" + NetSig::json_escape(cred) + "\"";
      json += "}";
    }
    json += "]";
    nwnet_set_ice(json.c_str());
  }

public:
  WebTransport() {
    nw_init();
    handle_ = nw_create();
  }

  ~WebTransport() { close(); }

  void start_host() override { nw_start_host(handle_); }

  void start_join(const std::string &remote_offer) override {
    nw_start_join(handle_, remote_offer.c_str());
  }

  bool local_description_ready() const override {
    return nw_desc_ready(handle_) != 0;
  }

  std::string local_description() const override {
    int len = nw_desc_len(handle_);
    if (len <= 0) return std::string();
    std::vector<char> buf(len + 1);
    nw_desc_copy(handle_, &buf[0], len + 1);
    return std::string(&buf[0]);
  }

  void set_remote_answer(const std::string &remote_answer) override {
    nw_set_answer(handle_, remote_answer.c_str());
  }

  void set_force_relay(bool on) override {
    nw_set_force_relay(handle_, on ? 1 : 0);
  }

  // ---- trickle ICE (M3-2b) ----
  void set_trickle(bool on) override { nw_set_trickle(handle_, on ? 1 : 0); }

  bool poll_local_candidate(std::string &out) override {
    int len = nw_cand_len(handle_);
    if (len < 0) return false;
    std::vector<char> buf(len + 1);
    nw_cand_take(handle_, &buf[0], len + 1);
    out.assign(&buf[0]);
    return true;
  }

  void add_remote_candidate(const std::string &mid,
                            const std::string &cand) override {
    nw_add_cand(handle_, mid.c_str(), cand.c_str());
  }

  bool connected() const override { return nw_connected(handle_) != 0; }

  std::string connection_info() const override {
    char buf[64];
    nw_path_info(handle_, buf, (int)sizeof(buf));
    return std::string(buf);
  }
  bool failed() const override { return nw_failed(handle_) != 0; }

  int buffered_amount() const override { return nw_buffered(handle_); }

  void send_reliable(const void *data, size_t size) override {
    nw_send(handle_, 1, data, (int)size);
  }

  void send_unreliable(const void *data, size_t size) override {
    nw_send(handle_, 0, data, (int)size);
  }

  bool poll(std::vector<unsigned char> &out) override {
    int size = nw_poll_size(handle_);
    if (size < 0) return false;
    out.resize(size);
    nw_poll_take(handle_, out.empty() ? (void *)0 : &out[0]);
    return true;
  }

  void close() override {
    if (handle_ > 0) {
      nw_close(handle_);
      handle_ = 0;
    }
  }

private:
  int handle_;
};

NetTransport *create_web_transport() {
  return new WebTransport();
}

// ---- clipboard helpers (async navigator.clipboard) ---------------------
// The read must be started inside a user gesture (the lobby's V-key
// handler) or the browser denies it; completion is polled per frame.

void net_clipboard_write(const std::string &text) {
  nw_init();
  nw_clip_write(text.c_str());
}

void net_clipboard_read_start() {
  nw_init();
  nw_clip_read_start();
}

bool net_clipboard_read_poll(std::string &out) {
  if (nw_clip_pending() != 0) return false;
  int len = nw_clip_len();
  if (len < 0) {
    out.clear();  // denied / nothing read
  } else {
    std::vector<char> buf(len + 1);
    nw_clip_copy(&buf[0], len + 1);
    out.assign(&buf[0]);
  }
  return true;
}

// ---- browser-console / automated test hooks ----------------------------
// The web equivalent of NEWTONIA_NET_SELFTEST: a blocking loopback is
// impossible on the single-threaded web main thread, so instead these
// export one driveable transport per page. Two tabs (or a Playwright
// test) call them to handshake and exchange bytes through the full C++
// WebTransport path. Example (page A hosts, page B joins):
//   A: Module._nwtest_create(); Module._nwtest_host();
//      ...wait Module._nwtest_desc_ready()...
//      offer = Module.ccall('nwtest_desc', 'string', [], [])
//   B: Module._nwtest_create();
//      Module.ccall('nwtest_join', null, ['string'], [offer])
//      ...wait desc_ready, pass answer back to A...
//   A: Module.ccall('nwtest_answer', null, ['string'], [answer])
//   both: wait Module._nwtest_connected(), then _nwtest_send/_nwtest_poll3.

static NetTransport *s_test_transport = 0;

extern "C" {

EMSCRIPTEN_KEEPALIVE void nwtest_create() {
  delete s_test_transport;
  s_test_transport = NetTransport::create();
}

EMSCRIPTEN_KEEPALIVE void nwtest_host() {
  if (s_test_transport) s_test_transport->start_host();
}

EMSCRIPTEN_KEEPALIVE void nwtest_join(const char *offer) {
  if (s_test_transport) s_test_transport->start_join(offer);
}

EMSCRIPTEN_KEEPALIVE int nwtest_desc_ready() {
  return s_test_transport && s_test_transport->local_description_ready();
}

EMSCRIPTEN_KEEPALIVE const char *nwtest_desc() {
  static std::string desc;
  desc = s_test_transport ? s_test_transport->local_description()
                          : std::string();
  return desc.c_str();
}

EMSCRIPTEN_KEEPALIVE void nwtest_answer(const char *answer) {
  if (s_test_transport) s_test_transport->set_remote_answer(answer);
}

EMSCRIPTEN_KEEPALIVE int nwtest_connected() {
  return s_test_transport && s_test_transport->connected();
}

EMSCRIPTEN_KEEPALIVE int nwtest_failed() {
  return s_test_transport && s_test_transport->failed();
}

// Sends a 3-byte message (a, b, c); rel selects the channel.
EMSCRIPTEN_KEEPALIVE void nwtest_send(int rel, int a, int b, int c) {
  if (!s_test_transport) return;
  unsigned char msg[3] = {(unsigned char)a, (unsigned char)b,
                          (unsigned char)c};
  if (rel)
    s_test_transport->send_reliable(msg, sizeof(msg));
  else
    s_test_transport->send_unreliable(msg, sizeof(msg));
}

// Pops one 3-byte message and returns it packed as a<<16|b<<8|c, or -1
// when nothing is pending (or the message isn't 3 bytes).
EMSCRIPTEN_KEEPALIVE int nwtest_poll3() {
  if (!s_test_transport) return -1;
  std::vector<unsigned char> msg;
  if (!s_test_transport->poll(msg) || msg.size() != 3) return -1;
  return ((int)msg[0] << 16) | ((int)msg[1] << 8) | (int)msg[2];
}

}  // extern "C"

#endif /* __EMSCRIPTEN__ */
