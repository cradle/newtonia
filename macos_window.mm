// macOS-specific window helpers.
#ifdef __APPLE__

#import <AppKit/AppKit.h>

// Force the application to the foreground so the window receives keyboard
// input immediately on launch (needed when launched from Steam, which keeps
// itself as the active app).
extern "C" void activate_app_macos() {
  [NSApp activateIgnoringOtherApps:YES];
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
