// COMPILE-SMOKE STUB — NOT the real GDK header.
//
// Used only by .github/workflows/xbox-console-smoke.yml, which compiles the
// _GAMING_XBOX code paths on a hosted runner where the NDA GDKX headers are
// unavailable.  This mirrors the API surface that xbox_main.cpp ASSUMES;
// it does not validate those assumptions.  Phase 3 of xbox/PORT_PLAN.md
// must verify them against the real GDKX (note: XSuspendResume.h is not in
// the public GDK docs — SDL's GDK backend uses
// RegisterAppStateChangeNotification from appnotify.h instead).
//
// Never add this directory to a real GDK build's include path.

#ifndef SMOKE_STUB_XSUSPENDRESUME_H
#define SMOKE_STUB_XSUSPENDRESUME_H

#include <windows.h>
#include "XTaskQueue.h"

extern "C" {

typedef unsigned int XSuspendResumeAcknowledgmentId;

typedef void CALLBACK XSuspendResumeCallback(
    void *context, XSuspendResumeAcknowledgmentId acknowledgmentId);

HRESULT XSuspendResumeRegisterForSuspend(
    XTaskQueueHandle queue,
    void *context,
    XSuspendResumeCallback *callback,
    XTaskQueueRegistrationToken *token);

void XSuspendResumeUnregisterForSuspend(XTaskQueueRegistrationToken *token);

void XSuspendResumeAcknowledge(XSuspendResumeAcknowledgmentId acknowledgmentId);

} // extern "C"

#endif // SMOKE_STUB_XSUSPENDRESUME_H
