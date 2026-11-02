// Kept separate from engine headers so Mach and SDL use their native types.
#include "Platform/IOSStartup.h"
#include "Platform/IOSStartupPump.h"
#include <SDL3/SDL.h>
#include <mach/mach.h>
#include <os/proc.h>
#include <cstdio>

namespace {
IOSStartupPump pump; // Only the main thread reads/mutates the policy.
Uint64 firstTrace = 0;
}

extern "C" void ZH_IOS_StartupTrace(const char *stage, const char *detail)
{
    if (!SDL_IsMainThread() || !pump.isActive())
        return;
    const Uint64 now = SDL_GetTicksNS();
    if (!firstTrace) firstTrace = now;
    task_vm_info_data_t memory = {};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    const kern_return_t status = task_info(mach_task_self(), TASK_VM_INFO,
        reinterpret_cast<task_info_t>(&memory), &count);
    const double footprint = status == KERN_SUCCESS ? memory.phys_footprint / 1048576.0 : -1;
    fprintf(stderr, "[IOS_INIT] elapsed_ms=%.1f stage=%s detail=%s footprint_mib=%.1f available_mib=%.1f\n",
        (now - firstTrace) / 1000000.0, stage ? stage : "", detail ? detail : "",
        footprint, os_proc_available_memory() / 1048576.0);
}

extern "C" void ZH_IOS_ServiceStartup()
{
    if (!SDL_IsMainThread()) return;
    if (!pump.begin(SDL_GetTicksNS(), true)) return;
    // Let UIKit finish its launch, layout and lifecycle callbacks while archives
    // load. Polling/dispatching game input here would re-enter incomplete subsystems.
    SDL_PumpEvents();
    pump.end();
}

extern "C" void ZH_IOS_StartupFinished()
{
    ZH_IOS_StartupTrace("ready", "engine initialization complete");
    pump.finish();
}
