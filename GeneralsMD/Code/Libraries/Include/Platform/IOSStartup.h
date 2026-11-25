// Startup-only iOS tracing and UIKit servicing. No engine work is dispatched here.
#pragma once
#if defined(ZH_PLATFORM_IOS)
extern "C" void ZH_IOS_StartupTrace(const char *stage, const char *detail);
extern "C" void ZH_IOS_ServiceStartup();
extern "C" void ZH_IOS_StartupFinished();
#else
inline void ZH_IOS_StartupTrace(const char *, const char *) {}
inline void ZH_IOS_ServiceStartup() {}
inline void ZH_IOS_StartupFinished() {}
#endif
