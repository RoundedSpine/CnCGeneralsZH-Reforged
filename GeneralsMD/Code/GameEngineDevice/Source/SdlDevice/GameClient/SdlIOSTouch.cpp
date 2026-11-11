#include "PreRTS.h"
#if defined(ZH_PLATFORM_IOS)
#include "SdlDevice/GameClient/IOSTouch.h"
#include "SdlDevice/GameClient/IOSTouchChrome.h"
#include "SdlDevice/GameClient/SdlInput.h"
#include "GameClient/Display.h"
#include "GameClient/LookAtXlat.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
IOSTouch::State gestures;
void (*touchFeedback)(int, float, float, float, float) = nullptr;
bool configured = false;
bool chromeInstalled = false;

void configure()
{
    if (configured) return;
    configured = true;
    if (const char *ms = std::getenv("ZH_IOS_HOLD_MS")) {
        const int value = std::atoi(ms);
        if (value > 0) gestures.setHoldDelay((std::uint64_t)value * 1000000ULL);
    }
    if (const char *mode = std::getenv("ZH_IOS_MOMENTUM")) {
        if (std::strcmp(mode, "off") == 0) gestures.setMomentum(IOSTouch::Momentum::Off);
        else if (std::strcmp(mode, "short") == 0) gestures.setMomentum(IOSTouch::Momentum::Short);
        else if (std::strcmp(mode, "normal") == 0) gestures.setMomentum(IOSTouch::Momentum::Normal);
    }
    if (const char *pan = std::getenv("ZH_IOS_ONE_FINGER_PAN"))
        gestures.setOneFingerPan(pan[0] == '1');
    fprintf(stderr, "[IOS_TOUCH] hold_ms=%llu momentum=%d one_finger_pan=%d\n",
        (unsigned long long)(gestures.holdDelayNs() / 1000000ULL), (int)gestures.momentumMode(),
        (int)gestures.oneFingerPanEnabled());
}

void dispatchAction(IOSTouch::Action action, Uint64 timestamp, SDL_WindowID windowID)
{
    SDL_Event event;
    SDL_zero(event);
    event.common.timestamp = timestamp;
    using IOSTouch::ActionKind;
    if (action.kind == ActionKind::HoldArmed) {
        SDL_Window *window = SDL_GetWindowFromID(windowID);
        int width = 0, height = 0;
        if (window) SDL_GetWindowSize(window, &width, &height);
        if (touchFeedback) touchFeedback(1, action.x, action.y, (float)width, (float)height);
        fprintf(stderr, "[IOS_TOUCH] hold_armed x=%.1f y=%.1f\n", action.x, action.y);
        return;
    }
    if (action.kind == ActionKind::PanFling) {
        if (!TheLookAtTranslator) return;
        SDL_Window *window = SDL_GetWindowFromID(windowID);
        int width = 0, height = 0;
        if (!window || !TheDisplay || !SDL_GetWindowSize(window, &width, &height) || width <= 0 || height <= 0) return;
        const float sx = (float)TheDisplay->getWidth() / width;
        const float sy = (float)TheDisplay->getHeight() / height;
        TheLookAtTranslator->beginTouchFling({action.x * sx, action.y * sy}, action.vx * sx, action.vy * sy,
            gestures.momentumTau());
        fprintf(stderr, "[IOS_TOUCH] fling vx=%.1f vy=%.1f\n", action.vx * sx, action.vy * sy);
        return;
    }
    if (action.kind == ActionKind::PanStart || action.kind == ActionKind::PanMove || action.kind == ActionKind::PanEnd) {
        if (!TheLookAtTranslator) return;
        if (action.kind == ActionKind::PanEnd) { TheLookAtTranslator->endTouchPan(); return; }
        SDL_Window *window = SDL_GetWindowFromID(windowID);
        int width = 0, height = 0;
        if (!window || !TheDisplay || !SDL_GetWindowSize(window, &width, &height) || width <= 0 || height <= 0) return;
        const float sx = (float)TheDisplay->getWidth() / width;
        const float sy = (float)TheDisplay->getHeight() / height;
        ScreenPan::Point from{action.x * sx, action.y * sy};
        if (action.kind == ActionKind::PanStart) TheLookAtTranslator->beginTouchPan(from);
        else TheLookAtTranslator->queueTouchPan(from, {(action.x + action.dx) * sx, (action.y + action.dy) * sy});
        return;
    } else if (action.kind == ActionKind::Escape) {
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.windowID = windowID;
        event.key.scancode = SDL_SCANCODE_ESCAPE;
        event.key.key = SDLK_ESCAPE;
        event.key.down = true;
        SdlInput_dispatch(event);
        event.type = SDL_EVENT_KEY_UP;
        event.key.down = false;
    } else if (action.kind == ActionKind::Move) {
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.windowID = windowID;
        event.motion.which = SDL_TOUCH_MOUSEID;
        event.motion.x = action.x;
        event.motion.y = action.y;
    } else if (action.kind == ActionKind::Wheel) {
        event.type = SDL_EVENT_MOUSE_WHEEL;
        event.wheel.windowID = windowID;
        event.wheel.which = SDL_TOUCH_MOUSEID;
        event.wheel.y = action.wheel;
        event.wheel.mouse_x = action.x;
        event.wheel.mouse_y = action.y;
    } else {
        const bool down = action.kind == ActionKind::LeftDown || action.kind == ActionKind::RightDown;
        event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.windowID = windowID;
        event.button.which = SDL_TOUCH_MOUSEID;
        event.button.button = action.kind == ActionKind::LeftDown || action.kind == ActionKind::LeftUp
            ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT;
        event.button.down = down;
        event.button.clicks = action.clicks > 0 ? action.clicks : 1;
        event.button.x = action.x;
        event.button.y = action.y;
        if (down && action.clicks >= 2)
            fprintf(stderr, "[IOS_TOUCH] double_click x=%.1f y=%.1f\n", action.x, action.y);
    }
    SdlInput_dispatch(event);
}
}

extern "C" void ZH_IOS_SetTouchFeedback(void (*fn)(int, float, float, float, float))
{
    touchFeedback = fn;
}

Bool SdlInput_hasIOSTouch() { return gestures.isActive(); }
Bool SdlInput_hasIOSMultiTouch() { return gestures.isMultiTouch(); }

void SdlInput_flushIOSTouch()
{
    configure();
    SDL_Window *window = SdlInput_gameWindow();
    if (!window) return;
    if (!chromeInstalled) {
        chromeInstalled = ZH_IOS_InstallTouchChrome(window) != 0;
    }
    const SDL_WindowID id = SDL_GetWindowID(window);
    const Uint64 now = SDL_GetTicksNS();
    // poll, not flush: a held finger produces no SDL event, and the hold arms from this clock.
    gestures.poll(now, [=](IOSTouch::Action action) { dispatchAction(action, now, id); });
}

void SdlInput_cancelIOSTouch()
{
    SDL_Window *window = SdlInput_gameWindow();
    const SDL_WindowID id = window ? SDL_GetWindowID(window) : 0;
    const Uint64 now = SDL_GetTicksNS();
    gestures.cancel([=](IOSTouch::Action action) { dispatchAction(action, now, id); });
    if (TheLookAtTranslator) TheLookAtTranslator->cancelTouchPan();
}

Bool SdlInput_dispatchIOSTouch(const SDL_Event &event)
{
    configure();
    IOSTouch::Kind kind;
    switch (event.type) {
        case SDL_EVENT_FINGER_DOWN: kind = IOSTouch::Kind::Down; break;
        case SDL_EVENT_FINGER_UP: kind = IOSTouch::Kind::Up; break;
        case SDL_EVENT_FINGER_MOTION: kind = IOSTouch::Kind::Move; break;
        case SDL_EVENT_FINGER_CANCELED: SdlInput_cancelIOSTouch(); return TRUE;
        default: return FALSE;
    }
    // A new finger stops a glide. The tap itself still goes through.
    if (kind == IOSTouch::Kind::Down && TheLookAtTranslator)
        TheLookAtTranslator->stopTouchFling();
    SDL_Window *window = SDL_GetWindowFromID(event.tfinger.windowID);
    if (!window) return TRUE;
    int width = 0, height = 0;
    if (!SDL_GetWindowSize(window, &width, &height) || width <= 0 || height <= 0) return TRUE;
    gestures.handle(kind, event.tfinger.touchID, event.tfinger.fingerID,
        event.tfinger.x * width, event.tfinger.y * height, event.tfinger.timestamp,
        [&](IOSTouch::Action action) { dispatchAction(action, event.tfinger.timestamp, event.tfinger.windowID); });
    return TRUE;
}
#endif
