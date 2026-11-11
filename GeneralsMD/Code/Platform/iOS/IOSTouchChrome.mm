// UIKit chrome for touch: the long-press haptic and ring, and the command strip.
// SDL owns the view controller, so preferredScreenEdgesDeferringSystemGestures
// cannot be turned on from here without replacing that controller. The home
// indicator can still take a swipe that starts on the bottom command card.
// Needs device validation.
#import <UIKit/UIKit.h>
#include <SDL3/SDL.h>
#include "SdlDevice/GameClient/IOSTouchChrome.h"
#include <cstdio>

static void (*controlHandler)(int) = nullptr;
static UIImpactFeedbackGenerator *holdHaptic = nil;
static UIWindow *hostWindow = nil;
static UIView *commandStrip = nil;

static UIWindow *windowFor(void *sdlWindow)
{
    if (!sdlWindow) return nil;
    SDL_PropertiesID props = SDL_GetWindowProperties((SDL_Window *)sdlWindow);
    return (UIWindow *)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL);
}

@interface ZHTouchChrome : NSObject
+ (void)pressed:(UIButton *)sender;
@end
@implementation ZHTouchChrome
+ (void)pressed:(UIButton *)sender
{
    if (sender.tag == ZH_IOS_CONTROL_SHIFT || sender.tag == ZH_IOS_CONTROL_CTRL) {
        sender.selected = !sender.selected;
        sender.backgroundColor = sender.selected ? [UIColor colorWithWhite:1 alpha:0.35] : [UIColor colorWithWhite:0 alpha:0.45];
    }
    if (controlHandler) controlHandler((int)sender.tag);
}
@end

static UIButton *makeButton(NSString *title, int tag, CGFloat y)
{
    UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
    button.frame = CGRectMake(0, y, 44, 44);
    button.tag = tag;
    [button setTitle:title forState:UIControlStateNormal];
    button.titleLabel.font = [UIFont boldSystemFontOfSize:15];
    [button setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
    button.backgroundColor = [UIColor colorWithWhite:0 alpha:0.45];
    button.layer.cornerRadius = 8;
    [button addTarget:[ZHTouchChrome class] action:@selector(pressed:) forControlEvents:UIControlEventTouchUpInside];
    return button;
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

extern "C" void ZH_IOS_SetControlHandler(void (*fn)(int))
{
    controlHandler = fn;
}

extern "C" int ZH_IOS_InstallTouchChrome(void *sdlWindow, int showBar)
{
    UIWindow *window = windowFor(sdlWindow);
    if (!window) return 0;
    hostWindow = window;
    ZH_IOS_SetTouchFeedback(ZH_IOS_TouchFeedbackThunk);
    const UIEdgeInsets inset = window.safeAreaInsets;
    fprintf(stderr, "[IOS_TOUCH] safe_area left=%.1f top=%.1f right=%.1f bottom=%.1f bar=%d\n",
        inset.left, inset.top, inset.right, inset.bottom, showBar);
    if (!showBar || commandStrip) return 1;
    const CGFloat height = window.bounds.size.height - inset.top - inset.bottom - 12;
    UIScrollView *strip = [[UIScrollView alloc] initWithFrame:CGRectMake(inset.left + 6, inset.top + 6, 44, height > 44 ? height : 44)];
    strip.autoresizingMask = UIViewAutoresizingFlexibleHeight | UIViewAutoresizingFlexibleRightMargin;
    strip.showsVerticalScrollIndicator = NO;
    strip.backgroundColor = [UIColor clearColor];
    const int tags[] = {
        ZH_IOS_CONTROL_SHIFT, ZH_IOS_CONTROL_CTRL, ZH_IOS_CONTROL_ATTACK_MOVE, ZH_IOS_CONTROL_STOP, ZH_IOS_CONTROL_GUARD,
        ZH_IOS_CONTROL_GROUP_1, ZH_IOS_CONTROL_GROUP_2, ZH_IOS_CONTROL_GROUP_3, ZH_IOS_CONTROL_GROUP_4, ZH_IOS_CONTROL_GROUP_5,
        ZH_IOS_CONTROL_GROUP_6, ZH_IOS_CONTROL_GROUP_7, ZH_IOS_CONTROL_GROUP_8, ZH_IOS_CONTROL_GROUP_9, ZH_IOS_CONTROL_GROUP_0
    };
    NSArray<NSString *> *titles = @[ @"Sh", @"Ct", @"A", @"S", @"G", @"1", @"2", @"3", @"4", @"5", @"6", @"7", @"8", @"9", @"0" ];
    for (NSUInteger i = 0; i < titles.count; ++i)
        [strip addSubview:makeButton(titles[i], tags[i], (CGFloat)i * 50)];
    strip.contentSize = CGSizeMake(44, titles.count * 50);
    [window addSubview:strip];
    commandStrip = strip;
    return 1;
}
