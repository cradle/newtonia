// macOS-specific window helpers.
#ifdef __APPLE__

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

// Force the application to the foreground so the window receives keyboard
// input immediately on launch (needed when launched from Steam, which keeps
// itself as the active app).
extern "C" void activate_app_macos() {
  if ([NSApp isActive]) return; // Already focused — nothing to do.
  if ([NSApp mainWindow]) {
    [[NSApp mainWindow] makeKeyAndOrderFront:nil];
  }
  if (@available(macOS 14.0, *)) {
    [NSApp activate];
  } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [NSApp activateIgnoringOtherApps:YES];
#pragma clang diagnostic pop
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
