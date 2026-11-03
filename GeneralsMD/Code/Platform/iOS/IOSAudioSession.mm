#import <AVFoundation/AVFoundation.h>
#include "Platform/IOSAudioSession.h"
#include "Platform/IOSLifecycle.h"
#include <atomic>
#include <cstdio>

namespace {
std::atomic<bool> watching{false};
std::atomic<unsigned long long> permission{0};
id observer = nil, lostObserver = nil, resetObserver = nil;
std::atomic<unsigned long long> servicesGeneration{0};
std::atomic<unsigned long long> interruptionState{0};
}

void ZH_IOS_AudioSessionWatch(bool enable)
{
    watching.store(false);
    if (observer) {
        [NSNotificationCenter.defaultCenter removeObserver:observer];
        observer = nil;
    }
    for(id token in @[lostObserver ?: NSNull.null,resetObserver ?: NSNull.null])
        if(token!=NSNull.null)[NSNotificationCenter.defaultCenter removeObserver:token];
    lostObserver=resetObserver=nil;
    const auto epoch = (permission.fetch_add(4) + 4) & ~3ULL;
    permission.store(epoch);
    interruptionState.store(epoch);
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
                if ((previous & ~3ULL) != epoch) return;
            } while (!permission.compare_exchange_weak(previous, epoch | (allowed ? 1ULL : 0ULL)));
            auto status=interruptionState.load();
            const auto bits=type==AVAudioSessionInterruptionTypeBegan?1ULL:(allowed?0ULL:2ULL);
            while((status&~3ULL)==epoch)
                if(interruptionState.compare_exchange_weak(status,epoch|bits))break;
            fprintf(stderr, "[IOS_AUDIO_SESSION] interruption=%s should_resume=%d foreground=%d\n",
                type == AVAudioSessionInterruptionTypeBegan ? "began" : "ended",
                allowed, ZH_IOS_IsForeground());
        }];
    lostObserver=[NSNotificationCenter.defaultCenter addObserverForName:AVAudioSessionMediaServicesWereLostNotification
        object:nil queue:nil usingBlock:^(NSNotification*) {
            auto previous=permission.load();
            while(watching.load()&&(previous&~3ULL)==epoch)
                if(permission.compare_exchange_weak(previous,epoch))break;
            fprintf(stderr,"[IOS_AUDIO_SESSION] media_services=lost\n");
        }];
    resetObserver=[NSNotificationCenter.defaultCenter addObserverForName:AVAudioSessionMediaServicesWereResetNotification
        object:nil queue:nil usingBlock:^(NSNotification*) {
            if(!watching.load())return;
            auto previous=permission.load();bool accepted=false;
            while((previous&~3ULL)==epoch) {
                const bool allowed=interruptionState.load()==epoch;
                if(permission.compare_exchange_weak(previous,epoch|(allowed?1ULL:0ULL))){accepted=true;break;}
            }
            if(accepted){servicesGeneration.fetch_add(1);fprintf(stderr,"[IOS_AUDIO_SESSION] media_services=reset rebuild_pending=1\n");}
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

unsigned long long ZH_IOS_AudioServicesGeneration(){return servicesGeneration.load();}
