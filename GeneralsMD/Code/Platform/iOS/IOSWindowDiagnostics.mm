#import <UIKit/UIKit.h>
#import <objc/runtime.h>
#include <SDL3/SDL.h>
#include "Platform/IOSWindowDiagnostics.h"
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <dlfcn.h>
#include <execinfo.h>

namespace {
std::atomic<unsigned> records{0};
void record(const char *stage, UIWindow *window, SDL_WindowID windowID, void *device)
{
    const unsigned sequence = records.fetch_add(1) + 1;
    if (sequence > 128) return;
    UIViewController *controller = window.rootViewController;
    UIView *view = controller.viewIfLoaded; // Do not load a view just to log it.
    const CGRect bounds = view ? view.bounds : CGRectZero;
    fprintf(stderr, "[IOS_WINDOW] seq=%u ticks_ns=%llu stage=%s sdl_window=%u gpu=%p native=%p "
        "hidden=%d key=%d controller=%p controller_class=%s view=%p view_class=%s attached=%d "
        "presenting=%d dismissing=%d transition=%d scene_state=%ld view_points=%.1fx%.1f scale=%.2f\n",
        sequence, (unsigned long long)SDL_GetTicksNS(), stage, windowID, device, (__bridge void *)window,
        window.hidden, window.isKeyWindow, (__bridge void *)controller,
        controller ? object_getClassName(controller) : "none", (__bridge void *)view,
        view ? object_getClassName(view) : "none", view.window == window && window != nil,
        controller.isBeingPresented, controller.isBeingDismissed, controller.transitionCoordinator != nil,
        window.windowScene ? (long)window.windowScene.activationState : -1L,
        (double)bounds.size.width, (double)bounds.size.height, (double)view.contentScaleFactor);
    // Image-relative PCs can be resolved using the exact build's Reforged-symbols.tsv.
    if (sequence <= 16) {
        void *frames[12];
        const int count = backtrace(frames, 12);
        for (int i = 0; i < count; ++i) {
            Dl_info info = {};
            if (!dladdr(frames[i], &info)) continue;
            const auto offset = (uintptr_t)frames[i] - (uintptr_t)info.dli_fbase;
            fprintf(stderr, "[IOS_WINDOW_STACK] seq=%u frame=%d image=%s offset=0x%llx symbol=%s\n",
                sequence, i, info.dli_fname ? info.dli_fname : "?", (unsigned long long)offset,
                info.dli_sname ? info.dli_sname : "?");
        }
    }
}
}

void ZH_IOS_TraceWindow(const char *stage, void *opaqueWindow, void *device)
{
    // All UIKit inspection stays on its owning thread.
    if (!SDL_IsMainThread()) {
        const unsigned sequence = records.fetch_add(1) + 1;
        if (sequence <= 128)
            fprintf(stderr, "[IOS_WINDOW] seq=%u stage=%s off_main_thread=1\n", sequence, stage);
        return;
    }
    SDL_Window *window = static_cast<SDL_Window *>(opaqueWindow);
    UIWindow *native = window ? (__bridge UIWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
        SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr) : nil;
    record(stage, native, window ? SDL_GetWindowID(window) : 0, device);
}

void ZH_IOS_InstallWindowDiagnostics()
{
    static bool installed = false;
    if (installed) return;
    installed = true;
    fprintf(stderr, "[IOS_SDL] compiled_version=%d runtime_version=%d ios=%s\n",
        SDL_VERSION, SDL_GetVersion(), UIDevice.currentDevice.systemVersion.UTF8String);
    for (NSNotificationName name in @[UIWindowDidBecomeVisibleNotification, UIWindowDidBecomeHiddenNotification,
            UIWindowDidBecomeKeyNotification, UIWindowDidResignKeyNotification]) {
        [NSNotificationCenter.defaultCenter addObserverForName:name object:nil queue:nil
            usingBlock:^(NSNotification *note) {
                if (![NSThread isMainThread]) return;
                if ([note.object isKindOfClass:UIWindow.class])
                    record(note.name.UTF8String, (UIWindow *)note.object, 0, nullptr);
            }]; // Kept by the notification center for the process lifetime.
    }
}
