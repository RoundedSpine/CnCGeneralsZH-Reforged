#!/usr/bin/env python3
"""Exercise the real smudge support gate without a GPU or engine initialization.

The legacy probe below the gate is represented by a counted callback: unsupported
platforms must cache NO before any rendering/readback, while other platforms must
still reach the probe. Platform macros are selected after host standard headers.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

CODE = Path(__file__).resolve().parents[1]


class SmudgeSupport(unittest.TestCase):
    def test_platform_gate_and_cached_result(self):
        source = (CODE / 'GameEngineDevice/Source/W3DDevice/GameClient/W3DSmudge.cpp').read_text()
        start = source.index('Bool W3DSmudgeManager::testHardwareSupport(void)')
        gate = source[start:source.index('IDirect3DTexture9 *backTexture=', start)]
        for platform, defines, unsupported in (
            ('ios', '#define ZH_PLATFORM_IOS 1\n#define __APPLE__ 1', True),
            ('linux', '#define __linux__ 1', True),
            ('macos', '#define __APPLE__ 1', False),
            ('windows', '#define _WIN32 1', False),
        ):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as folder:
                cpp, exe = Path(folder) / 'gate.cpp', Path(folder) / 'gate'
                cpp.write_text(r'''
#include <cassert>
using Bool = bool;
constexpr Bool FALSE = false;
enum { SMUDGE_SUPPORT_UNKNOWN, SMUDGE_SUPPORT_NO, SMUDGE_SUPPORT_YES };
struct W3DSmudgeManager {
    int m_hardwareSupportStatus = SMUDGE_SUPPORT_UNKNOWN;
    int probes = 0;
    Bool testHardwareSupport();
};
#undef __linux__
#undef __APPLE__
#undef _WIN32
#undef ZH_PLATFORM_IOS
''' + defines + '\n' + gate + r'''
        ++probes; // The legacy draw/readback probe must never be entered on iOS/Linux.
        m_hardwareSupportStatus = SMUDGE_SUPPORT_YES;
    }
    return m_hardwareSupportStatus == SMUDGE_SUPPORT_YES;
}
int main() {
    W3DSmudgeManager manager;
    for (int i = 0; i < 100; ++i) {
        assert(manager.testHardwareSupport() == !UNSUPPORTED);
        assert(manager.m_hardwareSupportStatus == (UNSUPPORTED ? SMUDGE_SUPPORT_NO : SMUDGE_SUPPORT_YES));
        assert(manager.probes == (UNSUPPORTED ? 0 : 1));
    }
    manager.m_hardwareSupportStatus = SMUDGE_SUPPORT_NO;
    assert(!manager.testHardwareSupport());
    assert(manager.probes == (UNSUPPORTED ? 0 : 1));
}
'''.replace('UNSUPPORTED', 'true' if unsupported else 'false'))
                subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', str(cpp), '-o', str(exe)], check=True)
                subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
