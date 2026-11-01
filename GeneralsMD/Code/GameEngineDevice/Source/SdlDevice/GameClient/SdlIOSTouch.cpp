#include "PreRTS.h"
#if defined(ZH_PLATFORM_IOS)
#include "SdlDevice/GameClient/IOSTouch.h"
#include "SdlDevice/GameClient/SdlInput.h"
#include <SDL3/SDL.h>

namespace {
IOSTouch::State gestures;
void dispatchAction(IOSTouch::Action action, Uint64 timestamp, SDL_WindowID windowID)
{
    SDL_Event event;
    SDL_zero(event);
    event.common.timestamp = timestamp;
    using IOSTouch::ActionKind;
    if (action.kind == ActionKind::Escape) {
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
        event.button.clicks = 1;
        event.button.x = action.x;
        event.button.y = action.y;
    }
    SdlInput_dispatch(event);
}
}

Bool SdlInput_hasIOSTouch() { return gestures.isActive(); }

void SdlInput_cancelIOSTouch()
{
    SDL_Window *window = SdlInput_gameWindow();
    const SDL_WindowID id = window ? SDL_GetWindowID(window) : 0;
    const Uint64 now = SDL_GetTicksNS();
    gestures.cancel([=](IOSTouch::Action action) { dispatchAction(action, now, id); });
}

Bool SdlInput_dispatchIOSTouch(const SDL_Event &event)
{
    IOSTouch::Kind kind;
    switch (event.type) {
        case SDL_EVENT_FINGER_DOWN: kind = IOSTouch::Kind::Down; break;
        case SDL_EVENT_FINGER_UP: kind = IOSTouch::Kind::Up; break;
        case SDL_EVENT_FINGER_MOTION: kind = IOSTouch::Kind::Move; break;
        case SDL_EVENT_FINGER_CANCELED: SdlInput_cancelIOSTouch(); return TRUE;
        default: return FALSE;
    }
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
