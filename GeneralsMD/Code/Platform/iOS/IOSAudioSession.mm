#import <AVFoundation/AVFoundation.h>
#include "Platform/IOSAudioSession.h"
#include "Platform/IOSLifecycle.h"
#include <atomic>
#include <cstdio>

namespace {
std::atomic<bool> watching{false};
std::atomic<unsigned long long> permission{0};
id observer = nil;
}

void ZH_IOS_AudioSessionWatch(bool enable)
{
    watching.store(false);
    if (observer) {
        [NSNotificationCenter.defaultCenter removeObserver:observer];
        observer = nil;
    }
    const auto epoch = (permission.fetch_add(2) + 2) & ~1ULL;
    permission.store(epoch);
    if (!enable) return;
    watching.store(true);
    observer = [NSNotificationCenter.defaultCenter
        addObserverForName:AVAudioSessionInterruptionNotification
        object:AVAudioSession.sharedInstance queue:nil usingBlock:^(NSNotification *note) {
            if (!watching.load()) return;
            const auto type = [note.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];
            const auto options = [note.userInfo[AVAudioSessionInterruptionOptionKey] unsignedIntegerValue];
            const bool allowed = type == AVAudioSessionInterruptionTypeEnded &&
                (options & AVAudioSessionInterruptionOptionShouldResume) != 0;
            auto previous = permission.load();
            do {
                if ((previous & ~1ULL) != epoch) return;
            } while (!permission.compare_exchange_weak(previous, epoch | (allowed ? 1ULL : 0ULL)));
            fprintf(stderr, "[IOS_AUDIO_SESSION] interruption=%s should_resume=%d foreground=%d\n",
                type == AVAudioSessionInterruptionTypeBegan ? "began" : "ended",
                allowed, ZH_IOS_IsForeground());
        }];
}

bool ZH_IOS_AudioCanResume()
{
    return watching.load() && (permission.load() & 1) && ZH_IOS_IsForeground();
}

bool ZH_IOS_ActivateAudioSession()
{
    if (!ZH_IOS_AudioCanResume()) return false;
    @autoreleasepool {
        NSError *error = nil;
        if (![AVAudioSession.sharedInstance setActive:YES error:&error]) {
            fprintf(stderr, "[IOS_AUDIO_SESSION] activation_failed code=%ld reason=%s\n",
                (long)error.code, error ? error.localizedDescription.UTF8String : "");
            return false;
        }
    }
    return ZH_IOS_AudioCanResume();
}
