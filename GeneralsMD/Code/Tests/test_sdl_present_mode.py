#!/usr/bin/env python3
"""Exercise the production swapchain policy with instrumented SDL boundaries."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

CODE = Path(__file__).resolve().parents[1]


class PresentMode(unittest.TestCase):
    def test_capabilities_failures_and_offscreen(self):
        frame = (CODE/'GameEngineDevice/Source/PosixDevice/Render/SdlGpuFrame.cpp').read_text()
        method = 'bool SdlGpuFrame::Configure_VSync(bool enabled)' + frame.split(
            'bool SdlGpuFrame::Configure_VSync(bool enabled)', 1)[1].split(
                '\nSdlGpuFrame::~SdlGpuFrame()', 1)[0]
        source = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
struct SDL_GPUDevice {};
struct SDL_Window {};
enum SDL_GPUPresentMode { SDL_GPU_PRESENTMODE_VSYNC, SDL_GPU_PRESENTMODE_IMMEDIATE, SDL_GPU_PRESENTMODE_MAILBOX };
constexpr int SDL_GPU_SWAPCHAINCOMPOSITION_SDR=0;
bool immediate=false,mailbox=false,flushOk=true,idleOk=true,setOk=true;
unsigned queries=0,flushes=0,waits=0,sets=0;
SDL_GPUPresentMode actual=SDL_GPU_PRESENTMODE_VSYNC;
bool SDL_WindowSupportsGPUPresentMode(SDL_GPUDevice*,SDL_Window*,SDL_GPUPresentMode mode){
    ++queries;return mode==SDL_GPU_PRESENTMODE_IMMEDIATE?immediate:mailbox;
}
bool SDL_WaitForGPUIdle(SDL_GPUDevice*){++waits;assert(flushes);return idleOk;}
bool SDL_SetGPUSwapchainParameters(SDL_GPUDevice*,SDL_Window*,int composition,SDL_GPUPresentMode mode){
    ++sets;assert(flushes&&waits&&composition==SDL_GPU_SWAPCHAINCOMPOSITION_SDR);
    assert(mode==SDL_GPU_PRESENTMODE_VSYNC||(mode==SDL_GPU_PRESENTMODE_IMMEDIATE?immediate:mailbox));
    if(!setOk)return false;
    actual=mode;return true;
}
const char *SDL_GetError(){return "injected failure";}
struct SdlGpuFrame {
    SDL_GPUDevice device;SDL_Window window;
    SDL_GPUDevice *GpuDevice=&device;SDL_Window *Window=&window;
    uint32_t PresentMode=SDL_GPU_PRESENTMODE_VSYNC;
    enum FlushCause {FlushUnclassified,FlushMaintenance};
    FlushCause lastFlushCause=FlushUnclassified;
    bool Flush(FlushCause cause=FlushUnclassified){lastFlushCause=cause;++flushes;return flushOk;}
    bool Configure_VSync(bool enabled);
};
''' + method + r'''
int main(){
    unsigned cases=0;
    for(unsigned caps=0;caps<4;++caps)for(unsigned start=0;start<3;++start)for(bool enabled:{false,true}){
        immediate=(caps&1)!=0;mailbox=(caps&2)!=0;
        SdlGpuFrame frame;frame.PresentMode=start;actual=SDL_GPUPresentMode(start);
        queries=flushes=waits=sets=0;
        const auto expected=enabled?SDL_GPU_PRESENTMODE_VSYNC:immediate?SDL_GPU_PRESENTMODE_IMMEDIATE:
            mailbox?SDL_GPU_PRESENTMODE_MAILBOX:SDL_GPU_PRESENTMODE_VSYNC;
        assert(frame.Configure_VSync(enabled));assert(frame.PresentMode==uint32_t(expected)&&actual==expected);
        assert(sets==unsigned(start!=uint32_t(expected))&&waits==sets&&flushes==sets);
        if(flushes)assert(frame.lastFlushCause==SdlGpuFrame::FlushMaintenance);
        assert(queries==(enabled?0u:immediate?1u:2u));
        assert(frame.Configure_VSync(enabled));assert(sets<=1); // Reapplying does not recreate the swapchain.
        ++cases;
    }
    immediate=true;mailbox=true;
    for(unsigned failure=0;failure<3;++failure){
        SdlGpuFrame frame;actual=SDL_GPU_PRESENTMODE_VSYNC;queries=flushes=waits=sets=0;
        flushOk=failure!=0;idleOk=failure!=1;setOk=failure!=2;
        assert(!frame.Configure_VSync(false));
        assert(frame.PresentMode==SDL_GPU_PRESENTMODE_VSYNC&&actual==SDL_GPU_PRESENTMODE_VSYNC);
        assert(flushes==1&&waits==unsigned(failure>0)&&sets==unsigned(failure>1));
        flushOk=idleOk=setOk=true;
        assert(frame.Configure_VSync(false));assert(actual==SDL_GPU_PRESENTMODE_IMMEDIATE);
        ++cases;
    }
    SdlGpuFrame offscreen;offscreen.Window=nullptr;queries=flushes=waits=sets=0;
    assert(offscreen.Configure_VSync(false)&&offscreen.Configure_VSync(true));
    assert(queries==0&&flushes==0&&waits==0&&sets==0);
    printf("%u presentation cases passed\n",cases+1);
}
'''
        source = '#include <initializer_list>\n' + source
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root/'test.cpp').write_text(source)
            exe = root/'present'
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra',
                            '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                            str(root/'test.cpp'), '-o', str(exe)], check=True)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('28 presentation cases passed', result.stdout)
            self.assertIn('mode=vsync fallback=1 applied=1', result.stderr)
            self.assertIn('mode=vsync fallback=0 applied=0 error=injected failure', result.stderr)


if __name__ == '__main__':
    unittest.main()
