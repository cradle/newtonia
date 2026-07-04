// Browser netplay backend: RTCPeerConnection via EM_JS glue on a
// Module.__nwnet object, polled from the main thread (same poll-style
// pattern as web_on_idb_ready in web_main.cpp). Compiled only under
// Emscripten; every other build sees an empty translation unit.

#ifdef __EMSCRIPTEN__

#include "net_transport.h"

// Phase 1 stub — the real backend (EM_JS peer connection, "rel"/"unrel"
// channels, inbox array, gesture-gated clipboard helpers) lands in
// Phase 3; see NETPLAY.md.
NetTransport* create_web_transport() {
  return nullptr;
}

#endif /* __EMSCRIPTEN__ */
