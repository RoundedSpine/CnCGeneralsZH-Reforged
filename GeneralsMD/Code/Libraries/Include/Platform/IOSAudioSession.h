// Native session policy, separate from engine headers and the audio callback.
#pragma once
#if defined(ZH_PLATFORM_IOS)
void ZH_IOS_AudioSessionWatch(bool enable);
bool ZH_IOS_AudioCanResume();
bool ZH_IOS_ActivateAudioSession();
#else
inline void ZH_IOS_AudioSessionWatch(bool) {}
inline bool ZH_IOS_AudioCanResume() { return true; }
inline bool ZH_IOS_ActivateAudioSession() { return true; }
#endif
