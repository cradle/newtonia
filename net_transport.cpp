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
