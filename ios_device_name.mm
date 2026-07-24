// LAN beacon device name (net_lan.cpp local_host_name()): iOS
// gethostname() is a useless "localhost", so the first on-device test
// beaconed the "NEWTONIA" fallback. Export UIDevice's name over the same
// NEWTONIA_DEVICE_NAME env bridge Android uses (NewtoniaActivity) —
// setenv with overwrite=0, so an externally set var (Xcode scheme) wins,
// mirroring the adb-extras override on Android. A Point-free TU like
// ios_share.mm: ios_main.mm cannot import UIKit (MacTypes.h's global
// `struct Point` collides with the game's `class Point`).
//
// iOS 16+ privacy note: UIDevice.name returns the generic model name
// ("iPhone") unless the app carries the Apple-approval-gated
// com.apple.developer.device-information.user-assigned-device-name
// entitlement. "IPHONE" still beats the fallback; if that entitlement is
// ever granted (requested like multicast was), the personal device name
// lights up here with no code change.

#ifdef __IOS__

#import <UIKit/UIKit.h>

#include <stdlib.h>

extern "C" void ios_export_device_name(void) {
    NSString *name = [UIDevice currentDevice].name;
    if (name.length == 0) return;
    setenv("NEWTONIA_DEVICE_NAME", name.UTF8String, 0);
}

#endif  // __IOS__
