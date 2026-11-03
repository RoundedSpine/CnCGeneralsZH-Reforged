#!/usr/bin/env python3
"""Drive the real lifecycle callback with a fake SDL event source, without UIKit."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

CODE = Path(__file__).resolve().parents[1]
SDL_HEADER = r'''
#pragma once
#define SDLCALL
using Uint64 = unsigned long long;
struct SDL_GPUDevice {};
struct SDL_GPUCommandBuffer {};
struct SDL_GPUFence {};
struct SDL_Event { unsigned type; };
enum { SDL_EVENT_WILL_ENTER_BACKGROUND=1, SDL_EVENT_DID_ENTER_BACKGROUND,
       SDL_EVENT_WILL_ENTER_FOREGROUND, SDL_EVENT_DID_ENTER_FOREGROUND,
       SDL_EVENT_LOW_MEMORY, SDL_EVENT_TERMINATING };
using SDL_EventFilter = bool (*)(void *, SDL_Event *);
bool SDL_AddEventWatch(SDL_EventFilter, void *);
bool SDL_IsMainThread();
void SDL_PumpEvents();
void SDL_Delay(unsigned);
bool SDL_WaitForGPUIdle(SDL_GPUDevice *);
const char *SDL_GetError();
bool SDL_SubmitGPUCommandBuffer(SDL_GPUCommandBuffer *);
SDL_GPUFence *SDL_SubmitGPUCommandBufferAndAcquireFence(SDL_GPUCommandBuffer *);
bool SDL_WaitForGPUFences(SDL_GPUDevice *,bool,SDL_GPUFence **,unsigned);
void SDL_ReleaseGPUFence(SDL_GPUDevice *,SDL_GPUFence *);
Uint64 SDL_GetTicksNS();
'''
PROGRAM = r'''
#include "Platform/IOSLifecycle.h"
#include <SDL3/SDL.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
SDL_EventFilter watch = nullptr;
int installs=0, drains=0, pumps=0, waits=0;
bool addWorks=true, background=false;
int submits=0;
struct SdlGpuFrame {
    bool SerializeSubmits=false;
    SDL_GPUDevice *GpuDevice=nullptr;
    double FenceMs=0;
    bool Submit(SDL_GPUCommandBuffer *commands);
};
bool SDL_SubmitGPUCommandBuffer(SDL_GPUCommandBuffer *) { assert(!background); ++submits; return true; }
SDL_GPUFence *SDL_SubmitGPUCommandBufferAndAcquireFence(SDL_GPUCommandBuffer *) {
    static SDL_GPUFence fence; assert(!background); ++submits; return &fence;
}
bool SDL_WaitForGPUFences(SDL_GPUDevice *,bool,SDL_GPUFence **,unsigned) { return true; }
void SDL_ReleaseGPUFence(SDL_GPUDevice *,SDL_GPUFence *) {}
Uint64 SDL_GetTicksNS() { return 0; }
// GPU_SUBMIT_IMPLEMENTATION
void emit(unsigned type) {
    if(type==SDL_EVENT_WILL_ENTER_BACKGROUND || type==SDL_EVENT_DID_ENTER_BACKGROUND) background=true;
    if(type==SDL_EVENT_DID_ENTER_FOREGROUND) background=false;
    SDL_Event event{type}; assert(watch(nullptr,&event));
}
bool SDL_AddEventWatch(SDL_EventFilter cb, void *) { ++installs; if (addWorks) watch=cb; return addWorks; }
bool SDL_IsMainThread() { return true; }
bool SDL_WaitForGPUIdle(SDL_GPUDevice *) { ++drains; return true; }
const char *SDL_GetError() { return "injected SDL failure"; }
void SDL_PumpEvents() {
    ++pumps;
    // Foreground preparation must not release rendering until activation.
    if (pumps == 1) emit(SDL_EVENT_WILL_ENTER_FOREGROUND);
    else { assert(pumps == 2); emit(SDL_EVENT_DID_ENTER_FOREGROUND); }
}
void SDL_Delay(unsigned) { ++waits; assert(waits <= 2); }
int main(int argc,char **argv) {
    if (argc > 1 && !strcmp(argv[1],"failure")) {
        addWorks=false; assert(!ZH_IOS_InstallLifecycle());
        addWorks=true; assert(ZH_IOS_InstallLifecycle()); assert(installs==2); return 0;
    }
    assert(ZH_IOS_InstallLifecycle()); assert(ZH_IOS_InstallLifecycle()); assert(installs==1);
    ZH_IOS_WaitForForeground(); assert(pumps==0 && ZH_IOS_IsForeground());
    if (argc > 1 && !strcmp(argv[1],"terminate")) {
        emit(SDL_EVENT_TERMINATING); ZH_IOS_WaitForForeground();
        return 9; // Termination must not resume engine/GPU work.
    }
    SDL_GPUDevice device;
    // Startup backgrounding before a device exists is safe.
    emit(SDL_EVENT_WILL_ENTER_BACKGROUND); assert(drains==0 && !ZH_IOS_IsForeground());
    ZH_IOS_WaitForForeground(); assert(pumps==2 && ZH_IOS_IsForeground());
    ZH_IOS_RegisterGPU(&device);
    pumps=waits=0;
    emit(SDL_EVENT_WILL_ENTER_BACKGROUND); assert(drains==1);
    emit(SDL_EVENT_DID_ENTER_BACKGROUND); assert(drains==1);
    ZH_IOS_WaitForForeground(); assert(pumps==2 && waits==2);
    pumps=waits=0;
    emit(SDL_EVENT_LOW_MEMORY); ZH_IOS_WaitForForeground();
    assert(pumps==0 && drains==1);
    // Exercise the renderer's actual Submit method in both modes. Submission
    // must wait until the real lifecycle callback has received activation.
    SdlGpuFrame frame; SDL_GPUCommandBuffer commands;
    for (bool serialize : {false,true}) {
        frame.SerializeSubmits=serialize; pumps=waits=0;
        emit(SDL_EVENT_WILL_ENTER_BACKGROUND);
        assert(frame.Submit(&commands)); assert(pumps==2 && !background);
    }
    assert(submits==2);
    const int oldDrains=drains;
    ZH_IOS_UnregisterGPU(&device);
    pumps=waits=0;
    emit(SDL_EVENT_WILL_ENTER_BACKGROUND); assert(drains==oldDrains);
    ZH_IOS_WaitForForeground(); assert(pumps==2);
    puts("lifecycle passed");
}
'''

class IOSLifecycle(unittest.TestCase):
    def test_real_callback_and_waits(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder); (root/'SDL3').mkdir()
            (root/'SDL3/SDL.h').write_text(SDL_HEADER)
            renderer=(CODE/'GameEngineDevice/Source/PosixDevice/Render/SdlGpuFrame.cpp').read_text()
            submit=renderer.split('bool SdlGpuFrame::Submit(SDL_GPUCommandBuffer * commands)',1)[1].split('\nvoid SdlGpuFrame::Set_Offscreen_Presents',1)[0]
            (root/'test.cpp').write_text(PROGRAM.replace('// GPU_SUBMIT_IMPLEMENTATION',
                'bool SdlGpuFrame::Submit(SDL_GPUCommandBuffer * commands)'+submit))
            exe=root/'lifecycle'
            subprocess.run([os.environ.get('CXX','c++'), '-std=c++17', '-Wall', '-Wextra',
                '-DZH_PLATFORM_IOS=1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                '-I'+str(root), '-I'+str(CODE/'Libraries/Include'), str(root/'test.cpp'),
                str(CODE/'Platform/iOS/IOSLifecycle.cpp'), '-o', str(exe)], check=True)
            for mode in ('normal','failure','terminate'):
                with self.subTest(mode=mode):
                    result=subprocess.run([str(exe), mode], capture_output=True, text=True, timeout=10)
                    self.assertEqual(result.returncode,0,result.stderr)
                    if mode=='normal':
                        self.assertIn('event=low-memory',result.stderr)
                        self.assertIn('lifecycle passed',result.stdout)
                    if mode=='terminate': self.assertIn('event=terminating',result.stderr)

if __name__ == '__main__': unittest.main()
