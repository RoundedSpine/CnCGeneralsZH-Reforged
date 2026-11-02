#!/usr/bin/env python3
"""Run the actual iOS entry adapter under a simulated UIKit caller.

SDL renames main to SDL_main and retains its UI loop when that function returns.
The game must finish cleanup before exiting; process status and exit handlers
must be preserved. No iOS SDK or UIKit window is needed to test this boundary.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

CODE = Path(__file__).resolve().parents[1]


class IOSMainExit(unittest.TestCase):
    def exercise(self, returning=False):
        source = (CODE / 'Main/PosixMain.cpp').read_text()
        adapter = source.split('// IOS_MAIN_EXIT_BEGIN\n',1)[1].split('// IOS_MAIN_EXIT_END',1)[0]
        if returning:
            # Negative control: reproduce SDL's default return-to-UIKit behavior.
            adapter = adapter.replace('std::exit( status );','return status;')
        prefix = r'''
#include <cstdio>
#include <cstdlib>
struct EngineCleanup { ~EngineCleanup() { puts("engine cleanup completed"); } };
struct ProcessCleanup { ~ProcessCleanup() { puts("process cleanup completed"); } } processCleanup;
static void onExit() { puts("atexit completed"); }
static int runIOSGameMain(int argc,char **argv) {
    EngineCleanup cleanup;
    std::atexit(onExit);
    return argc > 1 ? std::atoi(argv[1]) : 0;
}
#define main SDL_main
'''
        suffix = r'''
#undef main
int main(int argc,char **argv) {
    SDL_main(argc,argv);
    puts("UIKit loop remains alive with a black window");
    return 99;
}
'''
        with tempfile.TemporaryDirectory() as folder:
            cpp = Path(folder) / 'entry.cpp'
            exe = Path(folder) / 'entry'
            cpp.write_text(prefix + adapter + suffix)
            subprocess.run(['c++','-std=c++17','-Wall','-Wextra',str(cpp),'-o',str(exe)],check=True)
            return [subprocess.run([str(exe),str(status)],capture_output=True,text=True) for status in (0,7)]

    def test_exits_after_cleanup_and_preserves_status(self):
        for status, result in zip((0,7), self.exercise()):
            self.assertEqual(result.returncode,status)
            self.assertEqual(result.stdout.splitlines(),[
                'engine cleanup completed','atexit completed','process cleanup completed'])
            self.assertNotIn('UIKit loop remains alive',result.stdout)
            self.assertIn(f'engine cleanup completed status={status}',result.stderr)

    def test_returning_to_uikit_reproduces_the_black_window(self):
        for result in self.exercise(returning=True):
            self.assertEqual(result.returncode,99)
            self.assertIn('UIKit loop remains alive with a black window',result.stdout)


if __name__ == '__main__': unittest.main()
