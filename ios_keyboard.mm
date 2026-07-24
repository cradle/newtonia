// Soft-keyboard height for the lobby's CodeEntry touch layout
// (net_lobby.cpp soft_keyboard_fraction): iPhone landscape keyboards
// cover well past half the screen — taller than the fixed band layout
// measured on Android assumed — so the real frame is observed instead of
// guessed. A Point-free TU like ios_share.mm: UIKit's MacTypes.h Point
// collides with the game's Point, so no game headers here.

#ifdef __IOS__

#import <UIKit/UIKit.h>

// Written on the main thread by the notification blocks; read on the
// game loop, which SDL also runs on the main thread on iOS.
static float s_fraction = 0.0f;

extern "C" float ios_keyboard_cover_fraction(void) {
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
        [nc addObserverForName:UIKeyboardWillChangeFrameNotification
                        object:nil
                         queue:[NSOperationQueue mainQueue]
                    usingBlock:^(NSNotification *note) {
            CGRect end =
                [note.userInfo[UIKeyboardFrameEndUserInfoKey] CGRectValue];
            CGRect screen = [UIScreen mainScreen].bounds;
            CGRect vis = CGRectIntersection(screen, end);
            s_fraction = (CGRectIsNull(vis) || screen.size.height <= 0)
                             ? 0.0f
                             : (float)(vis.size.height / screen.size.height);
        }];
        [nc addObserverForName:UIKeyboardWillHideNotification
                        object:nil
                         queue:[NSOperationQueue mainQueue]
                    usingBlock:^(NSNotification *note) {
            s_fraction = 0.0f;
        }];
    });
    return s_fraction;
}

#endif  // __IOS__
