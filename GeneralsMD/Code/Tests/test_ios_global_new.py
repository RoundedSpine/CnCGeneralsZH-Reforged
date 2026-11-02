#!/usr/bin/env python3
"""Exercise GameMemory.cpp's actual global new/delete code in a portable ABI fixture.

The full engine alignment target is also enabled for iOS device compilation.
This host fixture isolates only the existing system implementation, so it needs
no Apple SDK or engine initialization. It tests zeroing, alignment and paired frees.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

CODE = Path(__file__).resolve().parents[1]


class IOSGlobalNew(unittest.TestCase):
    def test_ios_selects_aligned_zeroed_system_operators(self):
        source = (CODE / 'GameEngine/Source/Common/System/GameMemory.cpp').read_text()
        start = source.index('#if defined(ZH_SANITIZER_BUILD) || defined(__linux__)')
        guard = source[start:source.index('#ifndef ZH_SYSTEM_GLOBAL_NEW', start)]
        begin = source.index('// A sanitizer build or Linux: every form from calloc', start)
        body = source[begin:source.index('#endif // ZH_SYSTEM_GLOBAL_NEW', begin)]
        fixture = r'''
#include <cstdlib>
#include <new>
#include <cstdint>
#include <cassert>
#include <cstring>
// Header selection above uses the host platform. The allocator guard below must
// select its iOS path on its own, even when this test runs on Linux.
#undef __linux__
#define ZH_PLATFORM_IOS 1
#define WW_NOEXCEPT_DELETE noexcept
static int theLinkTester = 0;
static constexpr int ERROR_OUT_OF_MEMORY = 1;
'''
        fixture += guard + '\n#ifndef ZH_SYSTEM_GLOBAL_NEW\n#error iOS must use the system global operators\n#endif\n' + body
        fixture += r'''
int main() {
    const size_t sizes[] = {0,1,2,4,7,8,12,16,20,24,32,48,64,100,128,256,1024,4096,100000};
    for (size_t size : sizes) {
        void *blocks[] = { ::operator new(size), ::operator new[](size),
            ::operator new(size,std::nothrow), ::operator new[](size,std::nothrow),
            ::operator new(size,"test",1), ::operator new[](size,"test",1) };
        for (void *block : blocks) {
            assert(block && (reinterpret_cast<std::uintptr_t>(block) % __STDCPP_DEFAULT_NEW_ALIGNMENT__) == 0);
            for(size_t i=0;i<size;++i) assert(static_cast<unsigned char *>(block)[i] == 0);
            if(size) std::memset(block, 0xa5, size);
        }
        ::operator delete(blocks[0],size);
        ::operator delete[](blocks[1],size);
        ::operator delete(blocks[2],std::nothrow);
        ::operator delete[](blocks[3],std::nothrow);
        ::operator delete(blocks[4],"test",1);
        ::operator delete[](blocks[5],"test",1);
    }
    ::operator delete(nullptr);
    ::operator delete[](nullptr);
}
'''
        with tempfile.TemporaryDirectory() as folder:
            src = Path(folder) / 'operators.cpp'
            exe = Path(folder) / 'operators'
            src.write_text(fixture)
            command = ['c++', '-std=c++17', '-Wall', '-Wextra', '-fsanitize=address,undefined', str(src), '-o', str(exe)]
            # Avoid intermittent Linux ASan address-space collisions in PIE executables.
            import sys
            if sys.platform == 'linux': command += ['-fno-pie', '-no-pie']
            subprocess.run(command, check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
