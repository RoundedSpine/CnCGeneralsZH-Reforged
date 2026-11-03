#include "Platform/IOSLifecycle.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace {
enum class Phase { Foreground, Background, Terminating };
std::atomic<Phase> phase{Phase::Foreground};
// Device creation/destruction and UIKit's callbacks run on the main thread.
SDL_GPUDevice *gpu = nullptr;
bool installed = false;

bool SDLCALL applicationEvent(void *, SDL_Event *event)
{
    const char *name = nullptr;
    switch (event->type) {
        case SDL_EVENT_WILL_ENTER_BACKGROUND:
        case SDL_EVENT_DID_ENTER_BACKGROUND:
            if (phase.load() == Phase::Terminating) break;
            if (phase.exchange(Phase::Background) != Phase::Background && SDL_IsMainThread() && gpu) {
                // Finish previously submitted work before returning control to UIKit.
                if (!SDL_WaitForGPUIdle(gpu))
                    fprintf(stderr, "[IOS_LIFECYCLE] GPU drain failed: %s\n", SDL_GetError());
            }
            name = "background";
            break;
        case SDL_EVENT_DID_ENTER_FOREGROUND:
            if (phase.load() != Phase::Terminating) phase.store(Phase::Foreground);
            name = "foreground";
            break;
        case SDL_EVENT_LOW_MEMORY:
            // Do not free engine resources reentrantly, especially during startup.
            name = "low-memory";
            break;
        case SDL_EVENT_TERMINATING:
            phase.store(Phase::Terminating);
            if (SDL_IsMainThread() && gpu) SDL_WaitForGPUIdle(gpu);
            name = "terminating";
            break;
        default: break;
    }
    if (name) {
        fprintf(stderr, "[IOS_LIFECYCLE] event=%s type=%u ticks_ns=%llu gpu=%p main=%d\n",
            name, (unsigned)event->type, (unsigned long long)SDL_GetTicksNS(),
            static_cast<void *>(gpu), SDL_IsMainThread());
        fflush(stderr);
    }
    return true; // SDL delivers these app events directly to watchers, without queuing.
}
}

bool ZH_IOS_InstallLifecycle()
{
    if (installed) return true;
    installed = SDL_AddEventWatch(applicationEvent, nullptr);
    return installed;
}

bool ZH_IOS_IsForeground()
{
    return phase.load() == Phase::Foreground;
}

void ZH_IOS_WaitForForeground()
{
    while (phase.load() == Phase::Background) {
        // Process UIKit callbacks without dispatching queued game input. iOS may
        // suspend this thread here; foreground activation releases the wait.
        if (SDL_IsMainThread()) SDL_PumpEvents();
        SDL_Delay(20);
    }
    if (phase.load() == Phase::Terminating) {
        // The OS has requested termination. Avoid reentrant global/engine teardown
        // from a suspended or partially initialized state; diagnostics were flushed.
        std::_Exit(EXIT_SUCCESS);
    }
}

void ZH_IOS_RegisterGPU(void *device)
{
    ZH_IOS_WaitForForeground();
    gpu = static_cast<SDL_GPUDevice *>(device);
}

void ZH_IOS_UnregisterGPU(void *device)
{
    if (gpu == device) gpu = nullptr;
}
