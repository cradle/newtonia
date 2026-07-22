// Shared half of the signaling seam (see net_signal.h): JSON helpers,
// frame parsing, URL resolution and the relay self-test. The WebSocket
// itself lives in the per-platform backends (net_signal_rtc.cpp /
// net_signal_web.cpp).

#include "net_signal.h"
#include "net_protocol.h"
#include "preferences.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The baked default signaling worker (Glenn's production worker, deployed
// 2026-07-04 — signal/README.md). Overridable at COMPILE time; the RUNTIME
// overrides in net_signal_url() (the NEWTONIA_SIGNAL_URL env var, the prefs
// `signal_url`) still win over whatever is baked here:
//   -DNEWTONIA_SIGNAL_BETA=1  -> the isolated beta worker (its own Durable
//        Objects + secrets; deploy-signal auto-deploys it on master pushes
//        touching signal/**). deploy-ios's `signal_worker: beta` dispatch sets
//        this so a TestFlight build can be tested against beta WITHOUT touching
//        production. wrangler.toml [env.beta] name = newtonia-signal-beta.
//   -DNEWTONIA_SIGNAL_URL_DEFAULT='"wss://.../ws"'  -> any arbitrary worker
//        (the general escape hatch; quote the string for the preprocessor).
#ifndef NEWTONIA_SIGNAL_URL_DEFAULT
#  if defined(NEWTONIA_SIGNAL_BETA)
#    define NEWTONIA_SIGNAL_URL_DEFAULT "wss://newtonia-signal-beta.gfmcc.workers.dev/ws"
#  else
#    define NEWTONIA_SIGNAL_URL_DEFAULT "wss://newtonia-signal.gfmcc.workers.dev/ws"
#  endif
#endif
static const char *SIGNAL_URL_DEFAULT = NEWTONIA_SIGNAL_URL_DEFAULT;

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
  // M2 decision: the baked default is overridable via `signal_url` in the
  // preferences INI (self-hosted relays; the env var stays the dev override).
  if (!g_prefs.signal_url.empty()) return g_prefs.signal_url;
  return SIGNAL_URL_DEFAULT;
}

// The public site (GitHub Pages) that hosts the /join landing page and the
// browser game under /play. Keep in sync with web/site/CNAME.
static const char *JOIN_URL_BASE = "https://newtonia.metonymous.com/join?code=";

std::string net_join_url(const std::string &room_code) {
  return std::string(JOIN_URL_BASE) + room_code;
}

// No 0/O/1/I/5/S (confusable in the game font). No F: the desktop build
// toggles fullscreen on F at the window layer, before the lobby's code
// entry ever sees it — so codes simply never contain one (keep in sync
// with CODE_ALPHABET in signal/src/worker.js).
const char *NET_ROOM_CODE_ALPHABET = "ABCDEGHJKLMNPQRTUVWXYZ2346789";

bool net_room_code_char_ok(char c) {
  if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
  return c != '\0' && strchr(NET_ROOM_CODE_ALPHABET, c) != NULL;
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

std::string identity_frame(uint8_t platform, const std::string &name,
                           const std::string &cred) {
  std::string f = "{\"t\":\"identity\",\"platform\":" +
                  std::to_string((unsigned)platform) +
                  ",\"name\":\"" + json_escape(name) + "\"";
  if (!cred.empty()) f += ",\"cred\":\"" + json_escape(cred) + "\"";
  f += "}";
  return f;
}

// Locate the value text of a bare (unquoted) scalar field "key":<value>.
// Returns the [begin,end) span of the value, or false if the key is absent
// or immediately followed by a string (which json_field handles instead).
static bool scalar_span(const std::string &json, const char *key,
                        size_t &begin, size_t &end) {
  std::string needle = "\"";
  needle += key;
  needle += "\":";
  size_t at = json.find(needle);
  if (at == std::string::npos) return false;
  size_t i = at + needle.size();
  while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) i++;
  if (i >= json.size() || json[i] == '"') return false;  // a string value
  begin = i;
  while (i < json.size() && json[i] != ',' && json[i] != '}' &&
         json[i] != ' ' && json[i] != '\t')
    i++;
  end = i;
  return end > begin;
}

bool json_uint_field(const std::string &json, const char *key, unsigned &out) {
  size_t b, e;
  if (!scalar_span(json, key, b, e)) return false;
  unsigned v = 0;
  bool any = false;
  for (size_t i = b; i < e; i++) {
    if (json[i] < '0' || json[i] > '9') return false;
    v = v * 10 + (unsigned)(json[i] - '0');
    any = true;
  }
  if (!any) return false;
  out = v;
  return true;
}

bool json_bool_field(const std::string &json, const char *key, bool &out) {
  size_t b, e;
  if (!scalar_span(json, key, b, e)) return false;
  std::string v = json.substr(b, e - b);
  if (v == "true" || v == "1") { out = true; return true; }
  if (v == "false" || v == "0") { out = false; return true; }
  return false;
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

std::string offer_frame(const std::string &sdp) {
  return "{\"t\":\"offer\",\"pv\":\"" + std::to_string(Net::PROTO_VERSION) +
         "\",\"sdp\":\"" + json_escape(sdp) + "\"}";
}

std::string answer_frame(const std::string &sdp) {
  return "{\"t\":\"answer\",\"pv\":\"" + std::to_string(Net::PROTO_VERSION) +
         "\",\"sdp\":\"" + json_escape(sdp) + "\"}";
}

bool parse_frame(const std::string &frame, NetSignal::Event &ev) {
  std::string t;
  if (!json_field(frame, "t", t)) return false;
  ev.text.clear();
  ev.text2.clear();
  ev.platform = 0;       // Identity-only fields — reset so a prior event's
  ev.verified = false;   // values can never leak into a reused Event.
  if (t == "room") {
    ev.kind = NetSignal::Event::Room;
    json_field(frame, "token", ev.text2);  // reclaim token (M3-1)
    return json_field(frame, "code", ev.text);
  }
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
  if (t == "cand") {
    ev.kind = NetSignal::Event::Cand;
    json_field(frame, "mid", ev.text2);
    return json_field(frame, "cand", ev.text);
  }
  if (t == "offer") {
    ev.kind = NetSignal::Event::Offer;
    json_field(frame, "pv", ev.text2);  // peer PROTO_VERSION; empty = old build
    return json_field(frame, "sdp", ev.text);
  }
  if (t == "answer") {
    ev.kind = NetSignal::Event::Answer;
    json_field(frame, "pv", ev.text2);
    return json_field(frame, "sdp", ev.text);
  }
  if (t == "identity") {
    // Worker peer attestation (NETPLAY.md V0): {role, platform, name,
    // verified}. Name is optional (badge-only / role-labelled); platform and
    // verified are JSON scalars. The name is sanitized by the consumer.
    ev.kind = NetSignal::Event::Identity;
    json_field(frame, "role", ev.text2);
    json_field(frame, "name", ev.text);
    unsigned plat = 0;
    ev.platform = json_uint_field(frame, "platform", plat) ? (uint8_t)plat : 0;
    bool ver = false;
    ev.verified = json_bool_field(frame, "verified", ver) ? ver : false;
    return true;
  }
  if (t == "err")    { ev.kind = NetSignal::Event::Error;  json_field(frame, "reason", ev.text); return true; }
  if (t == "peer") {
    std::string e;
    if (!json_field(frame, "ev", e)) return false;
    if (e == "join") { ev.kind = NetSignal::Event::PeerJoin; return true; }
    if (e == "leave" || e == "host-lost") { ev.kind = NetSignal::Event::PeerLeave; return true; }
    return false;  // unknown peer events (e.g. host-back) are skipped
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

#if defined(NEWTONIA_NET_DISABLED)
// Force-disabled build (public web deploys): never opens a signal socket.
NetSignal *NetSignal::create() { return nullptr; }
#elif defined(NEWTONIA_NET_RTC)
NetSignal *net_signal_create_rtc();
NetSignal *NetSignal::create() { return net_signal_create_rtc(); }
#elif defined(__EMSCRIPTEN__)
NetSignal *net_signal_create_web();
NetSignal *NetSignal::create() { return net_signal_create_web(); }
#else
NetSignal *NetSignal::create() { return nullptr; }
#endif
