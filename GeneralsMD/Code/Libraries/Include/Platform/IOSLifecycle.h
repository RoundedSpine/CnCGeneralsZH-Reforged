// Application callbacks record lifecycle state without touching game subsystems.
#pragma once
#if defined(ZH_PLATFORM_IOS)
bool ZH_IOS_InstallLifecycle();
void ZH_IOS_WaitForForeground();
void ZH_IOS_RegisterGPU(void *device);
void ZH_IOS_UnregisterGPU(void *device);
#else
inline bool ZH_IOS_InstallLifecycle() { return true; }
inline void ZH_IOS_WaitForForeground() {}
inline void ZH_IOS_RegisterGPU(void *) {}
inline void ZH_IOS_UnregisterGPU(void *) {}
#endif
