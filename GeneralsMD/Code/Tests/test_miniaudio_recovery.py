#!/usr/bin/env python3
"""Exercise the real Miles adapter on miniaudio's null device with a session gate.

The shim replaces only the native session policy. Device operations, notifications,
service code and shutdown are real; UIKit/AVAudioSession still need device testing.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

CODE = Path(__file__).resolve().parents[1]
PROGRAM = r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>
#include "ADAPTER_SOURCE"
FILE *zh_fopen(const char *path, const char *mode) { return fopen(path, mode); }
std::atomic<bool> allowed{true}, activationWorks{true}, watched{false}, beginInActivation{false};
std::atomic<int> activations{0};
void ZH_IOS_AudioSessionWatch(bool enable) { watched=enable; }
bool ZH_IOS_AudioCanResume() { return allowed; }
void emit(ma_device_notification_type type) {
    ma_device_notification note{}; note.pDevice=&g_engine.device; note.type=type;
    assert(g_engine.device.onNotification);
    g_engine.device.onNotification(&note);
}
bool ZH_IOS_ActivateAudioSession() {
    ++activations;
    if (beginInActivation) emit(ma_device_notification_type_interruption_began);
    return activationWorks;
}
void stoppedForInterruption() {
    assert(ma_device_stop(&g_engine.device)==MA_SUCCESS);
    emit(ma_device_notification_type_interruption_began);
}
void tryRecovery() { g_engine.nextRecoveryAttempt={}; serviceAudioRecovery(); }
void interruptionDuringStart(const ma_device_notification *note) {
    deviceNotification(note);
    if(note->type==ma_device_notification_type_started)
        emit(ma_device_notification_type_interruption_began);
}
void newerInterruptionDuringStart(const ma_device_notification *note) {
    deviceNotification(note);
    if(note->type==ma_device_notification_type_started) {
        emit(ma_device_notification_type_interruption_began);
        emit(ma_device_notification_type_interruption_ended);
    }
}
void foregroundLostDuringStart(const ma_device_notification *note) {
    deviceNotification(note);
    if(note->type==ma_device_notification_type_started) allowed=false;
}
int main() {
    setenv("ZH_AUDIO_BACKEND","null",1);
    assert(AIL_startup() && AIL_quick_startup(1,0,44100,16,2));
    assert(watched && ma_device_is_started(&g_engine.device));
    // Deterministic checks of the same function the worker calls.
    g_engine.serviceRunning.store(false); g_engine.serviceThread.join();
    g_engine.serviceRunning.store(true);
    stoppedForInterruption(); serviceAudioRecovery();
    assert(!ma_device_is_started(&g_engine.device) && activations==0);
    emit(ma_device_notification_type_interruption_ended);
    allowed=false; serviceAudioRecovery();
    assert(!ma_device_is_started(&g_engine.device) && activations==0);
    allowed=true; activationWorks=false; tryRecovery();
    assert(!ma_device_is_started(&g_engine.device) && activations==1);
    serviceAudioRecovery(); assert(activations==1); // Retry is throttled.
    activationWorks=true; tryRecovery();
    assert(ma_device_is_started(&g_engine.device));
    assert(g_engine.recovery.load()==AudioRecovery::Healthy);
    // A new interruption while native session activation runs cancels restart.
    stoppedForInterruption(); emit(ma_device_notification_type_interruption_ended);
    beginInActivation=true; tryRecovery(); beginInActivation=false;
    assert(!ma_device_is_started(&g_engine.device));
    assert(g_engine.recovery.load()==AudioRecovery::Interrupted);
    // A begin event during ma_device_start must stop the just-started device.
    emit(ma_device_notification_type_interruption_ended);
    g_engine.device.onNotification=interruptionDuringStart;
    tryRecovery();
    assert(!ma_device_is_started(&g_engine.device));
    assert(g_engine.recovery.load()==AudioRecovery::Interrupted);
    g_engine.device.onNotification=deviceNotification;
    emit(ma_device_notification_type_interruption_ended);
    tryRecovery(); assert(ma_device_is_started(&g_engine.device));
    // A newer begin/end sequence cannot be consumed by an older restart.
    stoppedForInterruption(); emit(ma_device_notification_type_interruption_ended);
    g_engine.device.onNotification=newerInterruptionDuringStart;
    tryRecovery();
    assert(g_engine.recovery.load()==AudioRecovery::Pending);
    g_engine.device.onNotification=deviceNotification;
    tryRecovery(); assert(g_engine.recovery.load()==AudioRecovery::Healthy);
    // Foreground loss after the pre-start check keeps recovery pending.
    stoppedForInterruption(); emit(ma_device_notification_type_interruption_ended);
    g_engine.device.onNotification=foregroundLostDuringStart;
    tryRecovery();
    assert(!ma_device_is_started(&g_engine.device));
    assert(g_engine.recovery.load()==AudioRecovery::Pending);
    allowed=true; g_engine.device.onNotification=deviceNotification;
    tryRecovery(); assert(ma_device_is_started(&g_engine.device));
    // A real worker must also recover, without another startup call.
    g_engine.serviceThread=std::thread(serviceThreadMain);
    stoppedForInterruption(); emit(ma_device_notification_type_interruption_ended);
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while (!ma_device_is_started(&g_engine.device) && std::chrono::steady_clock::now()<until)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(ma_device_is_started(&g_engine.device));
    stoppedForInterruption(); emit(ma_device_notification_type_interruption_ended);
    AIL_shutdown(); assert(!watched && !g_engine.deviceReady && !g_engine.serviceThread.joinable());
    ma_device_notification late{}; late.pDevice=&g_engine.device;
    late.type=ma_device_notification_type_interruption_ended;
    deviceNotification(&late); // A late notification cannot undo shutdown.
    assert(g_engine.recovery.load()==AudioRecovery::Shutdown);
    // Offline rendering must never start a realtime device, including after a notification.
    g_engine.offline=true;
    assert(AIL_startup() && AIL_quick_startup(1,0,44100,16,2));
    stoppedForInterruption(); emit(ma_device_notification_type_interruption_ended);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    assert(!ma_device_is_started(&g_engine.device));
    AIL_shutdown();
    puts("audio recovery passed");
}
'''


class AudioRecovery(unittest.TestCase):
    def test_adapter_recovery_and_shutdown(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'Platform').mkdir()
            (root / 'Platform/IOSAudioSession.h').write_text('''#pragma once
void ZH_IOS_AudioSessionWatch(bool);
bool ZH_IOS_AudioCanResume();
bool ZH_IOS_ActivateAudioSession();
''')
            adapter = CODE / 'Libraries/Source/WWVegas/Miles6/miniaudio/miles_miniaudio.cpp'
            (root / 'test.cpp').write_text(PROGRAM.replace('ADAPTER_SOURCE', adapter.as_posix()))
            common = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                      '-DMA_ENABLE_ONLY_SPECIFIC_BACKENDS', '-DMA_ENABLE_NULL']
            audio = CODE / 'Libraries/Source/miniaudio'
            subprocess.run([os.environ.get('CC', 'cc'), *common, '-I'+str(audio),
                '-c', str(audio/'miniaudio.c'), '-o', str(root/'miniaudio.o')], check=True)
            exe = root / 'test'
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', *common,
                '-DZH_PLATFORM_IOS=1', '-I'+str(root), '-I'+str(audio),
                '-I'+str(CODE/'Libraries/Include'),
                '-I'+str(CODE/'Libraries/Source/WWVegas/WWLib'),
                '-I'+str(CODE/'Libraries/Source/WWVegas/Miles6/include'),
                str(root/'test.cpp'), str(root/'miniaudio.o'), '-pthread', '-lm',
                *([] if sys.platform == 'darwin' else ['-ldl']), '-o', str(exe)], check=True)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('audio recovery passed', result.stdout)


if __name__ == '__main__': unittest.main()
