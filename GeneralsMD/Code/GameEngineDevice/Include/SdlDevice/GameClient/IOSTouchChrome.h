// C interface between the gesture translator and the UIKit chrome (haptic, ring, command bar).
// The numeric control ids are the on-screen bar. IOSTouch::Controls and IOSTouchChrome.mm both use them.
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

enum ZH_IOS_Control {
    ZH_IOS_CONTROL_SHIFT = 1,
    ZH_IOS_CONTROL_CTRL = 2,
    ZH_IOS_CONTROL_ATTACK_MOVE = 3,
    ZH_IOS_CONTROL_STOP = 4,
    ZH_IOS_CONTROL_GUARD = 5,
    ZH_IOS_CONTROL_GROUP_1 = 6,
    ZH_IOS_CONTROL_GROUP_2 = 7,
    ZH_IOS_CONTROL_GROUP_3 = 8,
    ZH_IOS_CONTROL_GROUP_4 = 9,
    ZH_IOS_CONTROL_GROUP_5 = 10,
    ZH_IOS_CONTROL_GROUP_6 = 11,
    ZH_IOS_CONTROL_GROUP_7 = 12,
    ZH_IOS_CONTROL_GROUP_8 = 13,
    ZH_IOS_CONTROL_GROUP_9 = 14,
    ZH_IOS_CONTROL_GROUP_0 = 15
};

// kind 1 = long-press armed. x, y are window points. The function may be null in host tests.
void ZH_IOS_SetTouchFeedback(void (*fn)(int kind, float x, float y, float width, float height));
void ZH_IOS_SetControlHandler(void (*fn)(int control));
// sdlWindow is an SDL_Window*. showBar 0 leaves the bar off. Returns 1 once the
// UIKit window exists (the caller retries until then); 0 if it is not ready.
int ZH_IOS_InstallTouchChrome(void *sdlWindow, int showBar);

#ifdef __cplusplus
}
#endif
