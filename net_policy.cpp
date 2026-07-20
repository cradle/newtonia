#include "net_policy.h"

// Default backend: everything allowed, everywhere — shipping this seam
// changes no behavior on any platform. A platform backend TU defines
// NEWTONIA_NET_POLICY_BACKEND for the whole build and provides these two
// functions itself (the Xbox fork's private privilege-check backend).

#ifndef NEWTONIA_NET_POLICY_BACKEND

bool net_online_play_allowed() { return true; }

bool net_comms_allowed_with(const NetIdentity &) { return true; }

#endif  // NEWTONIA_NET_POLICY_BACKEND
