// Display safe-area insets (sensor housing / Dynamic Island / home
// indicator) for the HUD — the iOS half of the mechanism Android feeds
// from DisplayCutout (Overlay::set_safe_insets, view/overlay.h).
//
// Only matters now that the app rotates: portrait on a notched iPhone puts
// the camera exactly where the top HUD row (LEVEL / score / weapons) draws,
// which is the same collision the Android rotation work fixed. Landscape
// reports a top inset of 0 on every device, so the landscape layout that
// has always shipped is untouched.
//
// A Point-free TU like ios_share.mm / ios_keyboard.mm: UIKit pulls in
// MacTypes.h, whose global `struct Point` collides with the game's
// `class Point`, so no game headers may be visible here. Insets come back
// in UIKit POINTS; ios_main.mm scales them to the drawable's pixels, which
// is what Overlay expects.

#ifdef __IOS__

#import <UIKit/UIKit.h>

extern "C" void ios_safe_area_insets(float *top, float *bottom,
                                     float *left, float *right) {
    *top = *bottom = *left = *right = 0.0f;

    // The key window is SDL's (created in UIKit_CreateWindow before the
    // game loop starts); fall back to the first window so a call made
    // before it becomes key still measures something rather than lying
    // with zeros. safeAreaInsets already reflects the current orientation.
    UIWindow *win = nil;
    for (UIWindow *w in [UIApplication sharedApplication].windows) {
        if (w.isKeyWindow) { win = w; break; }
        if (!win) win = w;
    }
    if (!win) return;

    UIEdgeInsets in = win.safeAreaInsets;
    *top    = (float)in.top;
    *bottom = (float)in.bottom;
    *left   = (float)in.left;
    *right  = (float)in.right;
}

#endif /* __IOS__ */
