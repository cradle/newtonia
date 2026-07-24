// LAN beacon device name (net_lan.cpp local_host_name()): iOS
// gethostname() is a useless "localhost", so the first on-device test
// beaconed the "NEWTONIA" fallback. Export UIDevice's name over the same
// NEWTONIA_DEVICE_NAME env bridge Android uses (NewtoniaActivity) —
// setenv with overwrite=0, so an externally set var (Xcode scheme) wins,
// mirroring the adb-extras override on Android. A Point-free TU like
// ios_share.mm: ios_main.mm cannot import UIKit (MacTypes.h's global
// `struct Point` collides with the game's `class Point`).
//
// iOS 16+ privacy: UIDevice.name returns the generic model name
// ("iPhone") unless the app carries the Apple-approval-gated
// com.apple.developer.device-information.user-assigned-device-name
// entitlement — and Apple REJECTED that request (2026-07-24). The
// entitlement exists so an app can show the OWNER their own device names
// (telling their phone from their iPad); broadcasting a name to other
// people on the LAN is exactly what it is not for, so the personal
// device name is permanently off the table here — do not re-request.
//
// Instead the beacon upgrades to the public Game Center alias once
// sign-in resolves (the auth-change observer below): the alias is a name
// Apple already shows to other players, and it matches the LAN identity
// claim game_center_identity.mm renders in-game, so the lobby band and
// the in-game name agree. Sign-in usually lands seconds into launch,
// before a lobby can open; when it lands later, the still-unpaired
// lobby renames its beacon in place (NetLobby::lan_host_update ->
// Announce::set_host_name), and from pairing on the name is frozen for
// the session (GLGame::net_lan_beacon_name_) so rejoin-by-name can't
// miss on a drifted name. The generic "IPHONE" stays as the signed-out
// fallback.

#ifdef __IOS__

#import <UIKit/UIKit.h>
#if defined(GAME_CENTER_BUILD)
#import <GameKit/GameKit.h>
#endif

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

extern "C" void ios_export_device_name(void) {
    NSString *name = [UIDevice currentDevice].name;
    if (name.length > 0)
        setenv("NEWTONIA_DEVICE_NAME", name.UTF8String, 0);

#if defined(GAME_CENTER_BUILD)
    // Upgrade to the Game Center alias only over the generic model-name
    // export (UIDevice.name == UIDevice.model, the iOS 16+ shape): an
    // Xcode-scheme override and a real pre-iOS-16 device name (which
    // never equals the bare model — even French default names are
    // "iPhone de Glenn") both stay untouched.
    const char *cur = getenv("NEWTONIA_DEVICE_NAME");
    const char *model = [UIDevice currentDevice].model.UTF8String;
    if (cur && (!model || strcmp(cur, model) != 0)) return;

    // Registered before Achievements::init() triggers authentication, so
    // the first auth resolution is always caught. The observer lives for
    // the app's lifetime; the setenv happens on the main queue while the
    // menu idles, well before the game thread reads it at host start.
    [[NSNotificationCenter defaultCenter]
        addObserverForName:GKPlayerAuthenticationDidChangeNotificationName
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification *note) {
        (void)note;
        GKLocalPlayer *lp = [GKLocalPlayer localPlayer];
        if (!lp.isAuthenticated || lp.alias.length == 0) return;
        // Require a glyph local_host_name()'s sanitizer keeps, or the
        // beacon would collapse to the "NEWTONIA" fallback — a fully
        // non-Latin alias keeps the generic model name instead.
        const char *alias = lp.alias.UTF8String;
        bool drawable = false;
        for (size_t i = 0; alias && alias[i] && !drawable; i++)
            drawable = isalnum((unsigned char)alias[i]) != 0;
        if (drawable)
            setenv("NEWTONIA_DEVICE_NAME", alias, 1);
    }];
#endif  // GAME_CENTER_BUILD
}

#endif  // __IOS__
