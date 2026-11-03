// Observe window state at game-owned SDL boundaries without changing UIKit behavior.
#pragma once
#if defined(ZH_PLATFORM_IOS)
void ZH_IOS_InstallWindowDiagnostics();
void ZH_IOS_TraceWindow(const char *stage, void *window, void *device = nullptr);
#else
inline void ZH_IOS_InstallWindowDiagnostics() {}
inline void ZH_IOS_TraceWindow(const char *, void *, void * = nullptr) {}
#endif
