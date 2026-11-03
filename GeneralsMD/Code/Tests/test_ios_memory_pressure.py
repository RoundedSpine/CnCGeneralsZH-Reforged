#!/usr/bin/env python3
"""Exercise warning coalescing, failed trims and the native memory sampler."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

CODE = Path(__file__).resolve().parents[1]


class MemoryPressure(unittest.TestCase):
    def compile_run(self, source, native=False):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'test.cpp').write_text(source)
            args = [os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-DZH_PLATFORM_IOS=1', '-I'+str(root),
                    '-I'+str(CODE/'Libraries/Include'), str(root/'test.cpp')]
            if native:
                for name in ('SDL3', 'mach', 'os'):
                    (root/name).mkdir()
                (root/'SDL3/SDL.h').write_text('using Uint64=unsigned long long; Uint64 SDL_GetTicksNS();')
                (root/'mach/mach.h').write_text('''
using mach_msg_type_number_t=unsigned; using task_info_t=int*;
struct task_vm_info_data_t { unsigned long long phys_footprint; };
constexpr int TASK_VM_INFO=1, TASK_VM_INFO_COUNT=2, KERN_SUCCESS=0;
int mach_task_self(); int task_info(int,int,task_info_t,mach_msg_type_number_t*);
''')
                (root/'os/proc.h').write_text('unsigned long long os_proc_available_memory();')
                args += [str(CODE/'Platform/iOS/IOSMemoryDiagnostics.cpp')]
            exe = root/'memory'
            subprocess.run(args+['-o', str(exe)], check=True)
            return subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)

    def test_real_pressure_handler(self):
        frame = (CODE/'GameEngineDevice/Source/PosixDevice/Render/SdlGpuFrame.cpp').read_text()
        handler = frame.split('bool SdlGpuFrame::Handle_Memory_Pressure()', 1)[1].split(
            '\nbool SdlGpuFrame::Trim_Transient_Memory()', 1)[0]
        result = self.compile_run(r'''
#include <cassert>
#include <cstdio>
struct SdlGpuFrame {
    bool MemoryConstrained=false,MemoryTrimmed=false,flushWorks=true,trimWorks=true;
    unsigned SampleCount=4,disables=0,trims=0;
    bool Disable_Multisampling(const char*) {++disables;if(!flushWorks)return false;SampleCount=1;return true;}
    bool Trim_Transient_Memory(){++trims;return trimWorks;}
    bool Handle_Memory_Pressure();
};
bool SdlGpuFrame::Handle_Memory_Pressure()''' + handler + r'''
int main(){
    SdlGpuFrame frame;assert(frame.Handle_Memory_Pressure());
    assert(frame.MemoryConstrained&&frame.MemoryTrimmed&&frame.SampleCount==1);
    for(unsigned i=0;i<100;++i)assert(frame.Handle_Memory_Pressure());
    assert(frame.trims==1&&frame.disables==1);
    SdlGpuFrame failed;failed.flushWorks=false;assert(!failed.Handle_Memory_Pressure());
    assert(failed.trims==0&&!failed.MemoryTrimmed);
    failed.flushWorks=true;failed.trimWorks=false;assert(!failed.Handle_Memory_Pressure());
    assert(failed.SampleCount==1&&!failed.MemoryTrimmed);
    failed.trimWorks=true;assert(failed.Handle_Memory_Pressure());
    assert(failed.MemoryTrimmed&&failed.trims==2);
}
''')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_runtime_samples(self):
        result = self.compile_run(r'''
#include "Platform/IOSMemoryDiagnostics.h"
#include <SDL3/SDL.h>
#include <mach/mach.h>
#include <cassert>
Uint64 now=0;unsigned samples=0;bool readable=true;
Uint64 SDL_GetTicksNS(){return now;}
int mach_task_self(){return 0;}
int task_info(int,int,task_info_t out,mach_msg_type_number_t*){
    ++samples;auto *memory=reinterpret_cast<task_vm_info_data_t*>(out);
    memory->phys_footprint=500ULL*1048576;return readable?KERN_SUCCESS:1;
}
unsigned long long os_proc_available_memory(){return 128ULL*1048576;}
int main(){
    assert(ZH_IOS_MemoryFrame(1));assert(samples==1);
    now=29ULL*1000*1000*1000;assert(!ZH_IOS_MemoryFrame(2));assert(samples==1);
    ZH_IOS_TraceMemory("warning");assert(samples==2);
    now=30ULL*1000*1000*1000;assert(ZH_IOS_MemoryFrame(3));assert(samples==3);
    readable=false;ZH_IOS_TraceMemory("unavailable");assert(samples==4);
}
''', native=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('footprint_mib=500.0 available_mib=128.0 sampled_peak_mib=500.0', result.stderr)
        self.assertIn('stage=unavailable frame=0', result.stderr)
        self.assertIn('footprint_mib=-1.0', result.stderr)


if __name__ == '__main__':
    unittest.main()
