#!/usr/bin/env python3
"""Build and execute the actual host-tool CMake module with Ninja."""
from pathlib import Path
import os
import subprocess
import tempfile
import time
import unittest

MODULE = Path(__file__).resolve().parents[1] / 'CMake/HostTools.cmake'


class HostTools(unittest.TestCase):
    def run_command(self, *args):
        result = subprocess.run(args, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_generated_tool_runs_and_rebuilds(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder)
            build = source / 'build'
            tool = source / 'tool.cpp'
            def write_tool(value):
                tool.write_text('#include <cstdio>\nint main(int, char **argv) {'
                                f'FILE *f=fopen(argv[1],"w"); if(!f)return 1; fputs("{value}",f);'
                                'return fclose(f);}\n')
            write_tool('first')
            (source / 'CMakeLists.txt').write_text(f'''
cmake_minimum_required(VERSION 3.20)
project(HostToolRegression CXX)
set(ZH_HOST_CXX "${{CMAKE_CXX_COMPILER}}")
include("{MODULE.as_posix()}")
zh_add_host_executable(generator "${{CMAKE_CURRENT_SOURCE_DIR}}/tool.cpp")
add_custom_command(OUTPUT "${{CMAKE_CURRENT_BINARY_DIR}}/result"
  COMMAND $<TARGET_FILE:generator> "${{CMAKE_CURRENT_BINARY_DIR}}/result"
  DEPENDS generator VERBATIM)
add_custom_target(generated ALL DEPENDS "${{CMAKE_CURRENT_BINARY_DIR}}/result")
''')
            self.run_command('cmake', '-S', str(source), '-B', str(build), '-G', 'Ninja')
            self.run_command('cmake', '--build', str(build))
            self.assertEqual((build / 'result').read_text(), 'first')
            write_tool('changed')
            # Filesystems with coarse timestamps must still see this as newer.
            stamp = (build / 'host-tools/generator.stamp').stat().st_mtime + 2
            os.utime(tool, (stamp, stamp))
            self.run_command('cmake', '--build', str(build))
            self.assertEqual((build / 'result').read_text(), 'changed')
            # A missing byproduct must also be regenerated.
            (build / 'host-tools/generator').unlink()
            self.run_command('cmake', '--build', str(build))
            self.assertTrue((build / 'host-tools/generator').is_file())


if __name__ == '__main__':
    unittest.main()
