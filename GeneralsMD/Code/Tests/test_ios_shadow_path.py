#!/usr/bin/env python3
"""Exercise the actual shadow entry point as iOS, including capability refusal."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
CODE=Path(__file__).resolve().parents[1]

class IOSShadowPath(unittest.TestCase):
    def test_ios_uses_capability_checked_capture(self):
        source=(CODE/'GameEngineDevice/Source/PosixDevice/Render/PosixDevice9SunShadow.cpp').read_text()
        method='bool Begin(IDirect3DDevice9 *device, unsigned size)'+source.split(
            'bool Begin(IDirect3DDevice9 *device, unsigned size)',1)[1].split('\nbool End(',1)[0]
        program=r'''
#include <cassert>
#undef __linux__
#define ZH_PLATFORM_IOS 1
struct IDirect3DDevice9 {};
struct PosixDevice9 : IDirect3DDevice9 {
    bool supported=true; unsigned calls=0,lastSize=0;
    bool Begin_Sun_Shadow(unsigned size) { ++calls;lastSize=size;return supported; }
};
// METHOD
int main() {
    PosixDevice9 device;
    assert(!Begin(nullptr,2048));
    assert(Begin(&device,2048) && device.calls==1 && device.lastSize==2048);
    device.supported=false;
    assert(!Begin(&device,4096) && device.calls==2 && device.lastSize==4096);
}
'''
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder);(path/'test.cpp').write_text(program.replace('// METHOD',method))
            subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra',
                '-fsanitize=address,undefined',str(path/'test.cpp'),'-o',str(path/'test')],check=True)
            subprocess.run([str(path/'test')],check=True)

if __name__=='__main__':unittest.main()
