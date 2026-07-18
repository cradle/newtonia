// iOS share sheet (net_transport.h seam). Lives in its own translation
// unit: UIKit pulls in MacTypes.h, whose global `struct Point` collides
// with the game's `class Point`, so no game headers may be visible here
// (net_transport.h is Point-free).

#ifdef __IOS__

#import <UIKit/UIKit.h>

#include "net_transport.h"

bool net_share_available() { return true; }

void net_share_text(const std::string &text) {
    NSString *ns = [NSString stringWithUTF8String:text.c_str()];
    UIActivityViewController *avc =
        [[UIActivityViewController alloc] initWithActivityItems:@[ ns ]
                                          applicationActivities:nil];
    UIViewController *root = nil;
    for (UIWindow *w in [UIApplication sharedApplication].windows) {
        if (w.isKeyWindow) { root = w.rootViewController; break; }
    }
    if (!root) return;
    // iPad requires a popover anchor.
    avc.popoverPresentationController.sourceView = root.view;
    avc.popoverPresentationController.sourceRect =
        CGRectMake(root.view.bounds.size.width / 2,
                   root.view.bounds.size.height / 2, 1, 1);
    [root presentViewController:avc animated:YES completion:nil];
}

#endif /* __IOS__ */
