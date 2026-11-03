// Sampled process footprint/headroom, never an assertion of the actual peak.
#pragma once
#include <cstdint>
#if defined(ZH_PLATFORM_IOS)
void ZH_IOS_TraceMemory(const char *stage,std::uint64_t frame=0);
bool ZH_IOS_MemoryFrame(std::uint64_t frame);
#else
inline void ZH_IOS_TraceMemory(const char *,std::uint64_t=0) {}
inline bool ZH_IOS_MemoryFrame(std::uint64_t) {return false;}
#endif
