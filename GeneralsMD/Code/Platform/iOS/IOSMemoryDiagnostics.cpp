#include "Platform/IOSMemoryDiagnostics.h"
#include <SDL3/SDL.h>
#include <mach/mach.h>
#include <os/proc.h>
#include <atomic>
#include <cstdio>

// Runtime evidence is independent of the startup pump's lifetime. A process can
// be killed by jetsam without receiving a signal or running crash/exit handlers.
void ZH_IOS_TraceMemory(const char *stage,std::uint64_t frame)
{
    static std::atomic<std::uint64_t> sampledPeak{0};
    task_vm_info_data_t memory={};mach_msg_type_number_t count=TASK_VM_INFO_COUNT;
    const bool valid=task_info(mach_task_self(),TASK_VM_INFO,
        reinterpret_cast<task_info_t>(&memory),&count)==KERN_SUCCESS;
    std::uint64_t peak=sampledPeak.load();
    if(valid)while(peak<memory.phys_footprint&&!sampledPeak.compare_exchange_weak(peak,memory.phys_footprint)){}
    peak=sampledPeak.load();
    fprintf(stderr,"[IOS_MEMORY] stage=%s frame=%llu ticks_ns=%llu footprint_mib=%.1f available_mib=%.1f sampled_peak_mib=%.1f\n",
        stage?stage:"",(unsigned long long)frame,(unsigned long long)SDL_GetTicksNS(),
        valid?memory.phys_footprint/1048576.0:-1,os_proc_available_memory()/1048576.0,peak/1048576.0);
    fflush(stderr);
}
void ZH_IOS_MemoryFrame(std::uint64_t frame)
{
    static Uint64 nextTrace=0;const Uint64 now=SDL_GetTicksNS();
    if(now<nextTrace)return;
    nextTrace=now+30ULL*1000*1000*1000;
    ZH_IOS_TraceMemory("periodic",frame);
}
