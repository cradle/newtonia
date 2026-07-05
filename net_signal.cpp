// Shared half of the signaling seam (see net_signal.h): JSON helpers,
// frame parsing, URL resolution and the relay self-test. The WebSocket
// itself lives in the per-platform backends (net_signal_rtc.cpp /
// net_signal_web.cpp).

#include "net_signal.h"

#include <stdio.h>
#include <stdlib.h>

// Glenn's production worker (deployed 2026-07-04 — signal/README.md).
static const char *SIGNAL_URL_DEFAULT = "wss://newtonia-signal.gfmcc.workers.dev/ws";

#ifdef __EMSCRIPTEN__
std::string net_signal_url_web_override();  // net_signal_web.cpp
#endif

std::string net_signal_url() {
#ifdef __EMSCRIPTEN__
  std::string over = net_signal_url_web_override();
  if (!over.empty()) return over;
#endif
  const char *env = getenv("NEWTONIA_SIGNAL_URL");
  if (env && env[0]) return env;
  return SIGNAL_URL_DEFAULT;
}

bool net_room_code_char_ok(char c) {
  if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
  // No 0/O/1/I (confusable). No F: the desktop build toggles fullscreen on
  // F at the window layer, before the lobby's code entry ever sees it —
  // so codes simply never contain one (keep in sync with signal/src/worker.js).
  if (c == '0' || c == 'O' || c == '1' || c == 'I' || c == 'F') return false;
  return (c >= 'A' && c <= 'Z') || (c >= '2' && c <= '9');
}

namespace NetSig {

std::string json_escape(const std::string &s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (size_t i = 0; i < s.size(); i++) {
    unsigned char c = (unsigned char)s[i];
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += (char)c;
        }
    }
  }
  return out;
}

static bool unescape_into(const std::string &s, size_t begin, size_t end,
                          std::string &out) {
  out.clear();
  for (size_t i = begin; i < end; i++) {
    char c = s[i];
    if (c != '\\') { out += c; continue; }
    if (++i >= end) return false;
    switch (s[i]) {
      case '"':  out += '"'; break;
      case '\\': out += '\\'; break;
      case '/':  out += '/'; break;
      case 'n':  out += '\n'; break;
      case 'r':  out += '\r'; break;
      case 't':  out += '\t'; break;
      case 'b':  out += '\b'; break;
      case 'f':  out += '\f'; break;
      case 'u': {
        if (i + 4 >= end) return false;
        int v = 0;
        for (int k = 0; k < 4; k++) {
          char h = s[i + 1 + k];
          v <<= 4;
          if (h >= '0' && h <= '9') v |= h - '0';
          else if (h >= 'a' && h <= 'f') v |= h - 'a' + 10;
          else if (h >= 'A' && h <= 'F') v |= h - 'A' + 10;
          else return false;
        }
        i += 4;
        // The protocol is ASCII; anything beyond is replaced, not lost.
        out += (v < 0x80) ? (char)v : '?';
        break;
      }
      default: return false;
    }
  }
  return true;
}

bool json_field(const std::string &json, const char *key, std::string &out) {
  std::string needle = "\"";
  needle += key;
  needle += "\":\"";
  size_t at = json.find(needle);
  if (at == std::string::npos) return false;
  size_t begin = at + needle.size();
  size_t i = begin;
  while (i < json.size()) {
    if (json[i] == '\\') { i += 2; continue; }
    if (json[i] == '"') break;
    i++;
  }
  if (i >= json.size()) return false;
  return unescape_into(json, begin, i, out);
}

bool parse_frame(const std::string &frame, NetSignal::Event &ev) {
  std::string t;
  if (!json_field(frame, "t", t)) return false;
  ev.text.clear();
  if (t == "room")   { ev.kind = NetSignal::Event::Room;   return json_field(frame, "code", ev.text); }
  if (t == "joined") { ev.kind = NetSignal::Event::Joined; return true; }
  if (t == "ice") {
    std::string u, n, p;
    if (!json_field(frame, "urls", u)) return false;
    json_field(frame, "username", n);
    json_field(frame, "credential", p);
    ev.kind = NetSignal::Event::Ice;
    ev.text = u + "\n" + n + "\n" + p;
    return true;
  }
  if (t == "offer")  { ev.kind = NetSignal::Event::Offer;  return json_field(frame, "sdp", ev.text); }
  if (t == "answer") { ev.kind = NetSignal::Event::Answer; return json_field(frame, "sdp", ev.text); }
  if (t == "err")    { ev.kind = NetSignal::Event::Error;  json_field(frame, "reason", ev.text); return true; }
  if (t == "peer") {
    std::string e;
    if (!json_field(frame, "ev", e)) return false;
    ev.kind = (e == "join") ? NetSignal::Event::PeerJoin : NetSignal::Event::PeerLeave;
    return true;
  }
  return false;
}

}  // namespace NetSig

// ---- self-test -----------------------------------------------------------

#if defined(NEWTONIA_NET_RTC)

#include <SDL.h>

#include <chrono>
#include <thread>

namespace {
bool wait_event(NetSignal *s, NetSignal::Event::Kind want,
                NetSignal::Event &ev, int ms) {
  for (int i = 0; i < ms / 50; i++) {
    while (s->poll(ev)) {
      if (ev.kind == want) return true;
      if (ev.kind == NetSignal::Event::Error ||
          ev.kind == NetSignal::Event::Closed)
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return false;
}
}  // namespace

bool net_signal_selftest() {
  std::string url = net_signal_url();
  SDL_Log("net_signal_selftest: relay %s", url.c_str());

  NetSignal *host = NetSignal::create();
  NetSignal *join = NetSignal::create();
  if (!host || !join) { delete host; delete join; return false; }

  bool ok = false;
  NetSignal::Event ev;
  do {
    host->connect_host(url);
    if (!wait_event(host, NetSignal::Event::Room, ev, 10000)) break;
    std::string code = ev.text;
    SDL_Log("net_signal_selftest: room %s", code.c_str());

    join->connect_join(url, code);
    if (!wait_event(join, NetSignal::Event::Joined, ev, 10000)) break;

    host->send_offer("SELFTEST-OFFER\r\nv=0");
    if (!wait_event(join, NetSignal::Event::Offer, ev, 10000)) break;
    if (ev.text != "SELFTEST-OFFER\r\nv=0") break;

    join->send_answer("SELFTEST-ANSWER\r\nv=0");
    if (!wait_event(host, NetSignal::Event::Answer, ev, 10000)) break;
    if (ev.text != "SELFTEST-ANSWER\r\nv=0") break;

    ok = true;
  } while (0);

  host->close();
  join->close();
  delete host;
  delete join;
  SDL_Log("net_signal_selftest: %s", ok ? "PASS" : "FAIL");
  return ok;
}

#else

bool net_signal_selftest() { return false; }

#endif

// ---- factory ---------------------------------------------------------------

#if defined(NEWTONIA_NET_RTC)
NetSignal *net_signal_create_rtc();
NetSignal *NetSignal::create() { return net_signal_create_rtc(); }
#elif defined(__EMSCRIPTEN__)
NetSignal *net_signal_create_web();
NetSignal *NetSignal::create() { return net_signal_create_web(); }
#else
NetSignal *NetSignal::create() { return nullptr; }
#endif
