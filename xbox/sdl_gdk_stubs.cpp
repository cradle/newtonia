// No-op stubs for SDL2's GDK-specific _REAL functions.
//
// SDL2's dynapi jump table references these four symbols whenever __GDK__
// is defined at compile time.  The Gaming.Desktop.x64 and
// Gaming.Xbox.Scarlett.x64 MSBuild platforms define __GDK__ as a forced
// preprocessor symbol that cannot be suppressed from CMake (neither
// SDL_DISABLE_DYNAPI nor /U__GDK__ reach it reliably through the
// FetchContent sub-build).
//
// SDL2's own implementation lives in src/core/gdk/SDL_gdk.cpp, but that
// file is never added by SDL2's CMake because it cannot detect the GDK
// through our VS2022-platform approach (no GDKxPlatform.cmake toolchain).
//
// These stubs satisfy the linker.  The game does not call any of these
// SDL GDK APIs: GDK PLM lifecycle (suspend / resume) is handled directly
// in xbox_main.cpp via SDL_APP_WILLENTERBACKGROUND / DIDENTERFOREGROUND,
// and SDL_GDKRunApp is bypassed by SDL_MAIN_HANDLED.

#if defined(_GAMING_XBOX) || defined(_GAMING_DESKTOP)

// Forward-declare GDK opaque handle types without pulling in GDK headers.
struct XTaskQueue;
typedef XTaskQueue *XTaskQueueHandle;
struct XUser;
typedef XUser *XUserHandle;

extern "C" {

// SDL_GDKGetTaskQueue: retrieve the GDK task queue created by SDL.
// Not used in our build; return failure.
int SDL_GDKGetTaskQueue_REAL(XTaskQueueHandle *outTaskQueue)
{
    if (outTaskQueue) *outTaskQueue = nullptr;
    return -1;
}

// SDL_GDKRunApp: GDK-specific application entry point.
// Bypassed by SDL_MAIN_HANDLED; provided to satisfy the jump table.
// mainFunction signature matches SDL_main_func (int __cdecl(int, char**)).
int SDL_GDKRunApp_REAL(int (__cdecl *mainFunction)(int, char **), void *reserved)
{
    (void)reserved;
    return mainFunction ? mainFunction(0, nullptr) : -1;
}

// SDL_GDKSuspendComplete: signal that the title has finished suspending.
// Our event loop sends this implicitly; stub is a safe no-op.
void SDL_GDKSuspendComplete_REAL(void) {}

// SDL_GDKGetDefaultUser: retrieve the primary XUser for the title.
// Not used in our build; return failure.
int SDL_GDKGetDefaultUser_REAL(XUserHandle *outUserHandle)
{
    if (outUserHandle) *outUserHandle = nullptr;
    return -1;
}

} // extern "C"

#endif // _GAMING_XBOX || _GAMING_DESKTOP
