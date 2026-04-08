// macOS-specific window helpers.
#ifdef __APPLE__

#import <AppKit/AppKit.h>

// Force the application to the foreground so the window receives keyboard
// input immediately on launch (needed when launched from Steam, which keeps
// itself as the active app).
extern "C" void activate_app_macos() {
  [NSApp activateIgnoringOtherApps:YES];
}

#endif // __APPLE__
