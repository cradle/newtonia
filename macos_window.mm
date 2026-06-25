// macOS-specific window helpers.
#ifdef __APPLE__

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

// Force the application to the foreground so the window receives keyboard
// input immediately on launch (needed when launched from Steam, which keeps
// itself as the active app).
extern "C" void activate_app_macos() {
  // Step 1: unconditionally move every visible window above all other apps'
  // windows.  orderFrontRegardless works across application boundaries — it
  // raises our window above Steam's even while Steam is still the active
  // application.  makeKeyAndOrderFront:nil would be a no-op here because it
  // requires our app to already be frontmost.
  for (NSWindow *w in [NSApp windows]) {
    if (![w isVisible]) continue;
    [w orderFrontRegardless];
  }
  if ([NSApp isActive]) return; // Window already on top and app already active.
  // Step 2: make our application the active (frontmost) application so that
  // keyboard and mouse events are directed to our window.
  //
  // activateIgnoringOtherApps: is deprecated in macOS 14, but its
  // replacement [NSApp activate] does not force-steal focus from the
  // launching app on macOS 14/15 for non-sandboxed processes.  Steam keeps
  // itself active for several seconds after launching a native Steam game,
  // so we must use activateIgnoringOtherApps: (which still works on macOS
  // 14/15 for non-sandboxed apps) to reliably take the window to front.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  [NSApp activateIgnoringOtherApps:YES];
#pragma clang diagnostic pop
  // Step 3: now that the app is active, make each visible window the key
  // window so it receives keyboard events.
  for (NSWindow *w in [NSApp windows]) {
    if (![w isVisible]) continue;
    [w makeKeyAndOrderFront:nil];
  }
}

// --- macOS Game Mode / performance activity ---

// Holds the NSProcessInfo activity token for the lifetime of the game.
// Keeping it alive tells the OS to sustain high CPU/GPU priority and,
// on macOS 14+ (Sonoma), activates Game Mode (reduced Bluetooth latency,
// elevated scheduling priority).  NSAppSleepDisabled in Info.plist covers
// App Nap suppression on older releases.
static id<NSObject> s_game_activity = nil;

extern "C" void enable_game_mode_macos() {
  if (s_game_activity) return; // Already enabled.
  NSActivityOptions opts =
      NSActivityLatencyCritical |
      NSActivityUserInitiated |
      NSActivityIdleDisplaySleepDisabled;
  s_game_activity = [[NSProcessInfo processInfo]
      beginActivityWithOptions:opts
                        reason:@"Game"];
}

extern "C" int is_game_mode_active_macos() {
  return s_game_activity != nil ? 1 : 0;
}

// Reports how close we are to OS-level Game Mode being active:
//   0 = off     — no high-priority activity assertion is held at all.
//   1 = ready   — the assertion is held, but a precondition the OS requires
//                 for Game Mode is not met (pre-Sonoma, app not frontmost,
//                 or window not fullscreen), so Game Mode is NOT active.
//   2 = on      — assertion held AND every precondition met, so on macOS 14+
//                 the OS should have engaged Game Mode.
//
// Apple exposes no public API to read the OS Game Mode flag directly, so we
// report whether its documented preconditions are satisfied rather than the
// flag itself.
extern "C" int macos_game_mode_status() {
  if (!s_game_activity) return 0; // No performance assertion held.

  if (@available(macOS 14.0, *)) {
    // Game Mode only engages for the frontmost app...
    if (![NSApp isActive]) return 1;
    // ...running fullscreen.  GLUT's glutFullScreen() merely resizes the
    // window to the screen bounds rather than entering a fullscreen Space,
    // so accept either the native fullscreen style mask or a window whose
    // frame covers its entire screen.
    BOOL fullscreen = NO;
    for (NSWindow *w in [NSApp windows]) {
      if (![w isVisible]) continue;
      if ([w styleMask] & NSWindowStyleMaskFullScreen) { fullscreen = YES; break; }
      NSScreen *scr = [w screen];
      if (scr && NSEqualRects([w frame], [scr frame])) { fullscreen = YES; break; }
    }
    if (!fullscreen) return 1;
    return 2;
  }
  return 1; // Pre-Sonoma: activity held, but the OS has no Game Mode.
}

// --- Focus tracking via NSApplication notifications ---

static void (*s_focus_lost_cb)() = nullptr;
static void (*s_focus_gained_cb)() = nullptr;

@interface NewtFocusObserver : NSObject
@end

@implementation NewtFocusObserver
- (void)appDidResignActive:(NSNotification *)note {
  if (s_focus_lost_cb) s_focus_lost_cb();
}
- (void)appDidBecomeActive:(NSNotification *)note {
  if (s_focus_gained_cb) s_focus_gained_cb();
}
@end

static NewtFocusObserver *s_focus_observer = nil;

extern "C" void install_macos_focus_observer(void (*lost)(), void (*gained)()) {
  s_focus_lost_cb = lost;
  s_focus_gained_cb = gained;
  s_focus_observer = [[NewtFocusObserver alloc] init];
  [[NSNotificationCenter defaultCenter]
      addObserver:s_focus_observer
         selector:@selector(appDidResignActive:)
             name:NSApplicationDidResignActiveNotification
           object:nil];
  [[NSNotificationCenter defaultCenter]
      addObserver:s_focus_observer
         selector:@selector(appDidBecomeActive:)
             name:NSApplicationDidBecomeActiveNotification
           object:nil];
}

#endif // __APPLE__
