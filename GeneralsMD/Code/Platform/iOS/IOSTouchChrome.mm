// UIKit chrome for touch: the long-press haptic and ring.
// SDL owns the view controller, so preferredScreenEdgesDeferringSystemGestures
// cannot be turned on from here without replacing that controller. The home
// indicator can still take a swipe that starts on the bottom command card.
// Needs device validation.
#import <UIKit/UIKit.h>
#include <SDL3/SDL.h>
#include "SdlDevice/GameClient/IOSTouchChrome.h"
#include <cstdio>

static UIImpactFeedbackGenerator *holdHaptic = nil;
static UIWindow *hostWindow = nil;

static UIWindow *windowFor(void *sdlWindow)
{
    if (!sdlWindow) return nil;
    SDL_PropertiesID props = SDL_GetWindowProperties((SDL_Window *)sdlWindow);
    return (UIWindow *)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL);
}

extern "C" void ZH_IOS_TouchFeedbackThunk(int kind, float x, float y, float width, float height)
{
    (void)width; (void)height;
    if (kind != 1) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (!holdHaptic)
            holdHaptic = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleMedium];
        [holdHaptic prepare];
        [holdHaptic impactOccurred];
        if (!hostWindow) return;
        const CGFloat side = 36;
        UIView *ring = [[UIView alloc] initWithFrame:CGRectMake(x - side * 0.5f, y - side * 0.5f, side, side)];
        ring.userInteractionEnabled = NO;
        ring.backgroundColor = [UIColor colorWithWhite:1 alpha:0.18];
        ring.layer.cornerRadius = side * 0.5f;
        ring.layer.borderWidth = 2;
        ring.layer.borderColor = [UIColor colorWithWhite:1 alpha:0.9].CGColor;
        [hostWindow addSubview:ring];
        [UIView animateWithDuration:0.22 animations:^{ ring.alpha = 0; } completion:^(BOOL finished) {
            (void)finished;
            [ring removeFromSuperview];
        }];
    });
}

extern "C" int ZH_IOS_InstallTouchChrome(void *sdlWindow)
{
    UIWindow *window = windowFor(sdlWindow);
    if (!window) return 0;
    hostWindow = window;
    ZH_IOS_SetTouchFeedback(ZH_IOS_TouchFeedbackThunk);
    const UIEdgeInsets inset = window.safeAreaInsets;
    fprintf(stderr, "[IOS_TOUCH] safe_area left=%.1f top=%.1f right=%.1f bottom=%.1f\n",
        inset.left, inset.top, inset.right, inset.bottom);
    return 1;
}
