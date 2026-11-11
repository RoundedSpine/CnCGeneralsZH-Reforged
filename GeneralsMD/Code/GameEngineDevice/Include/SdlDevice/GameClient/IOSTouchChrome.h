// C interface between the gesture translator and the UIKit chrome (long-press haptic and ring).
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// kind 1 = long-press armed. x, y are window points. The function may be null in host tests.
void ZH_IOS_SetTouchFeedback(void (*fn)(int kind, float x, float y, float width, float height));
// sdlWindow is an SDL_Window*. Returns 1 once the UIKit window exists
// (the caller retries until then); 0 if it is not ready.
int ZH_IOS_InstallTouchChrome(void *sdlWindow);

#ifdef __cplusplus
}
#endif
