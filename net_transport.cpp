#include "net_transport.h"

// Backend factories live in their own translation units so that every
// platform's source glob can compile all net_*.cpp files unconditionally:
// net_transport_rtc.cpp is empty without NEWTONIA_NET_RTC, and
// net_transport_web.cpp is empty outside Emscripten.
#if defined(NEWTONIA_NET_RTC)
NetTransport* create_rtc_transport();
#elif defined(__EMSCRIPTEN__)
NetTransport* create_web_transport();
#endif

NetTransport* NetTransport::create() {
#if defined(NEWTONIA_NET_RTC)
  return create_rtc_transport();
#elif defined(__EMSCRIPTEN__)
  return create_web_transport();
#else
  return nullptr;
#endif
}

#ifndef __EMSCRIPTEN__

// Native clipboard is synchronous — the web versions (net_transport_web.cpp)
// are the reason this is an async-shaped three-call API.
#include <SDL.h>

void net_clipboard_write(const std::string& text) {
  SDL_SetClipboardText(text.c_str());
}

void net_clipboard_read_start() {}

bool net_clipboard_read_poll(std::string& out) {
  char* text = SDL_GetClipboardText();
  out = text ? text : "";
  SDL_free(text);
  return true;
}

#endif /* !__EMSCRIPTEN__ */

#ifndef NEWTONIA_NET_RTC

// No native backend: nothing to test. (The web backend cannot loopback
// in-process — the single-threaded browser can't block on its own event
// loop; the nwtest_* hooks in net_transport_web.cpp are its equivalent.)
bool net_selftest() { return false; }

#else

#include <SDL.h>

#include <cstring>
#include <chrono>
#include <thread>

static void selftest_sleep(int ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// Drives a full host/join handshake and a bidirectional echo over both
// channels using only the public NetTransport interface — the same calls
// the lobby and game will make.
bool net_selftest() {
  NetTransport* host = NetTransport::create();
  NetTransport* join = NetTransport::create();
  if (!host || !join) {
    delete host;
    delete join;
    return false;
  }

  static const unsigned char REL_MSG[] = {'R', 1, 2, 3};
  static const unsigned char UNREL_MSG[] = {'U', 9, 8, 7};

  bool pass = false;
  bool join_got_rel = false, join_got_unrel = false;
  bool host_got_rel = false, host_got_unrel = false;

  do {
    host->start_host();
    int waited = 0;
    while (!host->local_description_ready() && !host->failed() &&
           waited < 30000) {
      selftest_sleep(100);
      waited += 100;
    }
    if (!host->local_description_ready()) break;
    SDL_Log("net_selftest: host offer ready (%d ms)", waited);

    join->start_join(host->local_description());
    waited = 0;
    while (!join->local_description_ready() && !join->failed() &&
           waited < 30000) {
      selftest_sleep(100);
      waited += 100;
    }
    if (!join->local_description_ready()) break;
    SDL_Log("net_selftest: join answer ready (%d ms)", waited);

    host->set_remote_answer(join->local_description());
    waited = 0;
    while (!(host->connected() && join->connected()) && !host->failed() &&
           !join->failed() && waited < 30000) {
      selftest_sleep(100);
      waited += 100;
    }
    if (!(host->connected() && join->connected())) break;
    SDL_Log("net_selftest: connected (%d ms)", waited);

    // Bidirectional echo. Unreliable sends are repeated every 500 ms since
    // a maxRetransmits=0 channel guarantees nothing, even on loopback.
    host->send_reliable(REL_MSG, sizeof(REL_MSG));
    std::vector<unsigned char> msg;
    for (waited = 0; waited < 15000; waited += 50) {
      if (waited % 500 == 0) {
        if (!join_got_unrel) host->send_unreliable(UNREL_MSG, sizeof(UNREL_MSG));
        if (join_got_rel && !host_got_rel)
          join->send_reliable(REL_MSG, sizeof(REL_MSG));
        if (join_got_unrel && !host_got_unrel)
          join->send_unreliable(UNREL_MSG, sizeof(UNREL_MSG));
      }
      while (join->poll(msg)) {
        if (msg.size() == sizeof(REL_MSG) && msg[0] == 'R') join_got_rel = true;
        if (msg.size() == sizeof(UNREL_MSG) && msg[0] == 'U') join_got_unrel = true;
      }
      while (host->poll(msg)) {
        if (msg.size() == sizeof(REL_MSG) && msg[0] == 'R') host_got_rel = true;
        if (msg.size() == sizeof(UNREL_MSG) && msg[0] == 'U') host_got_unrel = true;
      }
      if (join_got_rel && join_got_unrel && host_got_rel && host_got_unrel)
        break;
      selftest_sleep(50);
    }
    pass = join_got_rel && join_got_unrel && host_got_rel && host_got_unrel;
  } while (0);

  SDL_Log("net_selftest: rel %d/%d unrel %d/%d host_failed=%d join_failed=%d",
          (int)join_got_rel, (int)host_got_rel, (int)join_got_unrel,
          (int)host_got_unrel, (int)host->failed(), (int)join->failed());

  host->close();
  join->close();
  delete host;
  delete join;
  return pass;
}

#endif /* NEWTONIA_NET_RTC */
