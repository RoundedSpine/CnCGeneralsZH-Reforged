#!/usr/bin/env python3
"""Run the production CreateDevice method with instrumented GPU/surface boundaries.

An unsupported surface must never claim the window. Also check cleanup after GPU
failure, successful ownership transfer, and headless/offscreen behavior.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

CODE = Path(__file__).resolve().parents[1]
PROGRAM = r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using RenderResult=int;
using RenderUInt32=unsigned;
using RenderWindow=void *;
enum D3DDEVTYPE { D3DDEVTYPE_HAL=1 };
constexpr int D3D_OK=0, D3DERR_INVALIDCALL=-1;
bool Render_Succeeded(int value) { return value>=0; }
bool Render_Failed(int value) { return value<0; }
struct D3DPRESENT_PARAMETERS {
    unsigned BackBufferWidth=100, BackBufferHeight=100, BackBufferFormat=22;
    unsigned AutoDepthStencilFormat=75, MultiSampleType=0, MultiSampleQuality=0;
    int EnableAutoDepthStencil=1;
    void *hDeviceWindow=nullptr;
};
int surfaceResult=0, gpuResult=0, claims=0, released=0, live=0, presents=0;
bool offscreenHint=false;
bool SDL_GetHintBoolean(const char *,bool) { return offscreenHint; }
const char *SDL_GetHint(const char *) { return "120"; }
struct PosixDirect3D9 {
    RenderResult CreateDevice(unsigned,D3DDEVTYPE,RenderWindow,RenderUInt32,
        D3DPRESENT_PARAMETERS *,void **);
};
struct PosixDevice9 {
    void *window;
    bool surfaces=false, gpu=false;
    PosixDevice9(PosixDirect3D9 *,void *w,D3DPRESENT_PARAMETERS &) : window(w) { ++live; }
    int Create_Implicit_Surfaces() { assert(!gpu); surfaces=surfaceResult>=0; return surfaceResult; }
    int Create_Gpu_Frame(bool offscreen) {
        assert(surfaces); // A real surface failure must not reach this boundary.
        if(window || offscreen) { ++claims; gpu=gpuResult>=0; return gpuResult; }
        return 0;
    }
    void Present_Offscreen(unsigned hz) { assert(surfaces && gpu && hz==120); ++presents; }
    void Release() { ++released; --live; delete this; }
};
// CREATE_DEVICE_IMPLEMENTATION
int main() {
    PosixDirect3D9 adapter;
    D3DPRESENT_PARAMETERS parameters;
    int window;
    void *device=&window;
    surfaceResult=-2;
    assert(adapter.CreateDevice(0,D3DDEVTYPE_HAL,&window,0,&parameters,&device)==-2);
    assert(device==nullptr && claims==0 && released==1 && live==0);
    surfaceResult=0; gpuResult=-3;
    assert(adapter.CreateDevice(0,D3DDEVTYPE_HAL,&window,0,&parameters,&device)==-3);
    assert(device==nullptr && claims==1 && released==2 && live==0);
    gpuResult=0;
    assert(adapter.CreateDevice(0,D3DDEVTYPE_HAL,&window,0,&parameters,&device)==0);
    assert(device!=nullptr && claims==2 && live==1 && presents==0);
    static_cast<PosixDevice9 *>(device)->Release();
    assert(live==0);
    // Headless makes surfaces but does not claim a window.
    assert(adapter.CreateDevice(0,D3DDEVTYPE_HAL,nullptr,0,&parameters,&device)==0);
    assert(claims==2 && presents==0);
    static_cast<PosixDevice9 *>(device)->Release();
    offscreenHint=true;
    assert(adapter.CreateDevice(0,D3DDEVTYPE_HAL,nullptr,0,&parameters,&device)==0);
    assert(claims==3 && presents==1);
    static_cast<PosixDevice9 *>(device)->Release();
    const int previousClaims=claims;
    parameters.BackBufferWidth=0; device=&window;
    assert(adapter.CreateDevice(0,D3DDEVTYPE_HAL,&window,0,&parameters,&device)==D3DERR_INVALIDCALL);
    assert(device==nullptr && claims==previousClaims && live==0);
    assert(adapter.CreateDevice(0,D3DDEVTYPE_HAL,&window,0,nullptr,&device)==D3DERR_INVALIDCALL);
    assert(adapter.CreateDevice(0,D3DDEVTYPE_HAL,&window,0,&parameters,nullptr)==D3DERR_INVALIDCALL);
    puts("device creation boundaries passed");
}
'''


class DeviceCreation(unittest.TestCase):
    def test_failure_never_claims_window_and_ownership_is_preserved(self):
        source = (CODE/'GameEngineDevice/Source/PosixDevice/Render/PosixDirect3D9.cpp').read_text()
        # The real method is the last definition in this translation unit.
        method = 'RenderResult PosixDirect3D9::CreateDevice' + source.split(
            'RenderResult PosixDirect3D9::CreateDevice', 1)[1]
        method = method.replace('IDirect3DDevice9 **device', 'void **device')
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            (root/'test.cpp').write_text(PROGRAM.replace('// CREATE_DEVICE_IMPLEMENTATION', method))
            exe=root/'test'
            subprocess.run([os.environ.get('CXX','c++'), '-std=c++17', '-Wall', '-Wextra',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(root/'test.cpp'),
                '-o', str(exe)], check=True)
            result=subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('device creation boundaries passed',result.stdout)
            self.assertIn('stage=implicit-surfaces result=0xfffffffe',result.stderr)
            self.assertIn('stage=gpu-frame result=0xfffffffd',result.stderr)


if __name__ == '__main__': unittest.main()
