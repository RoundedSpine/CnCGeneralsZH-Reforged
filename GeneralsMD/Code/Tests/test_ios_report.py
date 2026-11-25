#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import tempfile
import unittest
spec = importlib.util.spec_from_file_location('report', Path(__file__).resolve().parents[1] / 'Tools/publish-ios-report.py')
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)

class Report(unittest.TestCase):
    def test_early_error_survives_warning_tail(self):
        with tempfile.TemporaryDirectory() as folder:
            log = Path(folder) / 'build.log'
            log.write_text('FAILED: early.cpp.o\nearly.cpp:7: error: unavailable API\n'
                           + 'ordinary warning\n' * 10000 + 'finished\n')
            tail, errors = report.collect_log(log)
            self.assertNotIn('unavailable API', tail)
            self.assertIn('unavailable API', errors)
            self.assertEqual(len(tail.splitlines()),6000)
            self.assertEqual(len(report.collect_log(log,True)[0].splitlines()),300)

    def test_enum_error_names_are_not_compiler_errors(self):
        with tempfile.TemporaryDirectory() as folder:
            log = Path(folder) / 'build.log'
            log.write_text('case e_qrnoerror:\nreturn "noerror";\n')
            self.assertEqual(report.collect_log(log)[1].strip(),'')

if __name__ == '__main__': unittest.main()
