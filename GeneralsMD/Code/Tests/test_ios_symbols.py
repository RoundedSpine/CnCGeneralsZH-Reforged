#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('symbols',Path(__file__).resolve().parents[1]/'Tools/ios-symbols.py')
symbols=importlib.util.module_from_spec(spec);spec.loader.exec_module(symbols)
class Symbols(unittest.TestCase):
    def test_aslr_independent_offsets_and_local_functions(self):
        data='''0000000100000040 t __ZL10local_funcv
0000000100000000 T __mh_execute_header
0000000100000080 T __Z10draw_passv
0000000100001000 D _data
                 U _undefined
'''
        self.assertEqual(symbols.parse_nm(data),[(0,'__mh_execute_header'),(0x40,'__ZL10local_funcv'),(0x80,'__Z10draw_passv')])
    def test_missing_base_is_rejected(self):
        with self.assertRaises(ValueError): symbols.parse_nm('0000000100000080 T _draw\n')
if __name__=='__main__': unittest.main()
