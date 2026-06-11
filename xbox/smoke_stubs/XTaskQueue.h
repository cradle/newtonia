// COMPILE-SMOKE STUB — NOT the real GDK header.
//
// Used only by .github/workflows/xbox-console-smoke.yml; mirrors the
// XTaskQueue API surface that xbox_main.cpp uses.  See XSuspendResume.h in
// this directory for the full rationale.  Never add this directory to a
// real GDK build's include path.

#ifndef SMOKE_STUB_XTASKQUEUE_H
#define SMOKE_STUB_XTASKQUEUE_H

#include <windows.h>
#include <stdint.h>

extern "C" {

struct XTaskQueueObject;
typedef XTaskQueueObject *XTaskQueueHandle;

enum XTaskQueueDispatchMode {
  XTaskQueueDispatchMode_Manual,
  XTaskQueueDispatchMode_ThreadPool,
  XTaskQueueDispatchMode_SerializedThreadPool,
  XTaskQueueDispatchMode_Immediate
};

struct XTaskQueueRegistrationToken {
  uint64_t token;
};

HRESULT XTaskQueueCreate(XTaskQueueDispatchMode workDispatchMode,
                         XTaskQueueDispatchMode completionDispatchMode,
                         XTaskQueueHandle *queue);

void XTaskQueueCloseHandle(XTaskQueueHandle queue);

} // extern "C"

#endif // SMOKE_STUB_XTASKQUEUE_H
