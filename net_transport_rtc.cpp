// Native netplay backend over the libdatachannel C API (rtc/rtc.h).
// The whole file compiles away unless NEWTONIA_NET_RTC is defined, which
// only xbox/CMakeLists.txt does (GDK Desktop with NEWTONIA_NET=ON) — every
// other build glob picks this file up as an empty translation unit.

#ifdef NEWTONIA_NET_RTC

#include "net_transport.h"

// Phase 1 stub — the real backend (peer connection, "rel"/"unrel" channels,
// mutex-guarded inbound deque fed from libdatachannel's worker-thread
// callbacks) lands in Phase 2; see NETPLAY.md.
NetTransport* create_rtc_transport() {
  return nullptr;
}

#endif /* NEWTONIA_NET_RTC */
