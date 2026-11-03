#!/usr/bin/env python3
"""Native smudges follow live snapshot capability; Windows keeps its cached probe."""
from pathlib import Path
import subprocess
import tempfile
import unittest
CODE = Path(__file__).resolve().parents[1]

class SmudgeSupport(unittest.TestCase):
    def test_native_capability_and_windows_probe(self):
        source=(CODE/'GameEngineDevice/Source/W3DDevice/GameClient/W3DSmudge.cpp').read_text()
        start=source.index('Bool W3DSmudgeManager::testHardwareSupport(void)')
        gate=source[start:source.index('IDirect3DTexture9 *backTexture=',start)]
        for windows in (False,True):
            with self.subTest(windows=windows),tempfile.TemporaryDirectory() as folder:
                path=Path(folder)
                program=r'''
#include <cassert>
using Bool=bool;constexpr Bool FALSE=false;
enum{SMUDGE_SUPPORT_UNKNOWN,SMUDGE_SUPPORT_NO,SMUDGE_SUPPORT_YES};
namespace SceneEffects{bool available=true;bool SupportsHeatHaze(){return available;}}
struct W3DSmudgeManager{int m_hardwareSupportStatus=SMUDGE_SUPPORT_UNKNOWN,probes=0;Bool testHardwareSupport();};
#undef _WIN32
// PLATFORM
// GATE
        ++probes;m_hardwareSupportStatus=SMUDGE_SUPPORT_YES;
    }
    return m_hardwareSupportStatus==SMUDGE_SUPPORT_YES;
#endif
}
int main(){
    W3DSmudgeManager manager;assert(manager.testHardwareSupport());
    assert(manager.testHardwareSupport());
#if defined(_WIN32)
    assert(manager.probes==1&&manager.m_hardwareSupportStatus==SMUDGE_SUPPORT_YES);
    manager.m_hardwareSupportStatus=SMUDGE_SUPPORT_NO;assert(!manager.testHardwareSupport());
#else
    assert(manager.probes==0&&manager.m_hardwareSupportStatus==SMUDGE_SUPPORT_UNKNOWN);
    SceneEffects::available=false;assert(!manager.testHardwareSupport());
    SceneEffects::available=true;assert(manager.testHardwareSupport());
#endif
}
'''.replace('// PLATFORM','#define _WIN32 1' if windows else '').replace('// GATE',gate)
                (path/'test.cpp').write_text(program)
                subprocess.run(['c++','-std=c++17','-Wall','-Wextra',str(path/'test.cpp'),'-o',str(path/'test')],check=True)
                subprocess.run([str(path/'test')],check=True)
if __name__=='__main__':unittest.main()
