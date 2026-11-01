#!/usr/bin/env python3
"""The shared license gate must read desktop and iOS bundle link rules."""
from pathlib import Path
import subprocess
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1] / 'Tools'


class PackageLinkLibraries(unittest.TestCase):
    def linked(self, target):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            ninja = folder / 'build.ninja'
            paths = folder / 'paths'
            ninja.write_text(f'build {target}: CXX_EXECUTABLE_LINKER__generals_Release objects.o | libsdldevice.a\n'
                             '  LINK_LIBRARIES = libsdldevice.a SDL3/libSDL3.a -framework Metal ffmpeg/lib/libavcodec.a\n'
                             'build another: CXX_EXECUTABLE_LINKER__other objects.o\n'
                             '  LINK_LIBRARIES = libunrelated.a\n')
            result = subprocess.run(['bash', '-c', '. "$1"; package_link_libraries "$2" "$3"',
                                     'test', str(TOOLS / 'package-common.sh'), str(ninja), str(paths)],
                                    text=True, capture_output=True, check=True)
            return result.stdout.splitlines(), paths.read_text().splitlines()

    def test_desktop(self):
        names, paths = self.linked('generals')
        self.assertEqual(names, ['SDL3', 'avcodec', 'sdldevice'])
        self.assertEqual(paths, ['SDL3/libSDL3.a', 'ffmpeg/lib/libavcodec.a', 'libsdldevice.a'])

    def test_ios_bundle(self):
        self.assertEqual(self.linked('generals.app/generals'), self.linked('generals'))

    def test_new_dependency_requires_license(self):
        result = subprocess.run(['bash', '-c',
                                 '. "$1"; package_license_entries "$2" SDL3 unknownlibrary',
                                 'test', str(TOOLS / 'package-common.sh'),
                                 str(TOOLS / 'macos-app-licenses.txt')], text=True, capture_output=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn('unknownlibrary', result.stdout)


if __name__ == '__main__':
    unittest.main()
