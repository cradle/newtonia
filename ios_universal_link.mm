// iOS Universal Link handler for the Invites seam (invites.h). A tapped
// https://newtonia.metonymous.com/join?code=XXXX opens the app here and the
// room code is fed to Invites::note_accepted, so Menu::tick joins it — the
// same handoff Steam rich-presence and the cold-launch path use. A custom
// newtonia://…?code= scheme is handled too as a belt-and-braces fallback.
//
// Lives in its own translation unit (like ios_share.mm): UIKit pulls in
// MacTypes.h, whose global `struct Point` collides with the game's
// `class Point`, so no game headers may be visible here. invites.h is
// Point-free (only <string>).
//
// SDL2 owns the UIApplicationDelegate on iOS (SDLUIKitDelegate). We subclass
// it and override +getAppDelegateClassName via a category on the SDL class,
// so UIKit instantiates our subclass; it forwards everything to super and only
// adds the two link callbacks. This is the documented SDL pattern for custom
// iOS URL handling (the same hook games use for push/URL schemes).
//
// Requires (Glenn / release config, not code):
//   * the App ID's "Associated Domains" capability enabled in the portal, and
//     the `applinks:newtonia.metonymous.com` entitlement (ios/Entitlements*.plist),
//   * the site serving /.well-known/apple-app-site-association naming
//     <TeamID>.cc.gfm.Newtonia for the /join path.

#ifdef __IOS__

#import <UIKit/UIKit.h>

#include "invites.h"

// Pull the "code" query item out of a join URL and hand it to the shared
// invite layer. Works for both the https Universal Link and the custom scheme.
static void accept_join_url(NSURL *url) {
    if (!url) return;
    NSURLComponents *comps = [NSURLComponents componentsWithURL:url
                                       resolvingAgainstBaseURL:NO];
    for (NSURLQueryItem *item in comps.queryItems) {
        if ([item.name isEqualToString:@"code"] && item.value.length > 0) {
            Invites::note_accepted([item.value UTF8String]);
            return;
        }
    }
}

// The SDL app delegate we subclass. SDL's own header (SDL_uikitappdelegate.h)
// is internal and not on the public include path, so we declare the interface
// ourselves — the runtime class name is what has to match.
@interface SDLUIKitDelegate : UIResponder <UIApplicationDelegate>
@end

@interface NewtoniaAppDelegate : SDLUIKitDelegate
@end

@implementation NewtoniaAppDelegate

// Universal Link: an https://newtonia.metonymous.com/join?code= tapped while
// the app is installed and the domain is verified. Delivered here on both cold
// launch (after SDL's didFinishLaunching) and while already running.
- (BOOL)application:(UIApplication *)application
continueUserActivity:(NSUserActivity *)userActivity
 restorationHandler:(void (^)(NSArray<id<UIUserActivityRestoring>> *))restorationHandler {
    if ([userActivity.activityType isEqualToString:NSUserActivityTypeBrowsingWeb])
        accept_join_url(userActivity.webpageURL);
    return YES;
}

// Custom-scheme / direct-URL fallback (newtonia://join?code=…).
- (BOOL)application:(UIApplication *)app
            openURL:(NSURL *)url
            options:(NSDictionary<UIApplicationOpenURLOptionsKey, id> *)options {
    accept_join_url(url);
    return YES;
}

@end

// Redirect SDL to instantiate our subclass as the application delegate.
@interface SDLUIKitDelegate (Newtonia)
@end

@implementation SDLUIKitDelegate (Newtonia)
+ (NSString *)getAppDelegateClassName {
    return @"NewtoniaAppDelegate";
}
@end

#endif /* __IOS__ */
