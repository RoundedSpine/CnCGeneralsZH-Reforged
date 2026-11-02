#!/usr/bin/env python3
"""Keep image-relative text symbols beside the IPA for device-log symbolication."""
from pathlib import Path
import subprocess
import sys


def parse_nm(text):
    base = None
    symbols = []
    for line in text.splitlines():
        fields = line.split(None, 2)
        if len(fields) != 3: continue
        try: address = int(fields[0], 16)
        except ValueError: continue
        kind, name = fields[1:]
        if name in ('_mh_execute_header', '__mh_execute_header'): base = address
        if kind in ('T', 't'): symbols.append((address, name))
    if base is None: raise ValueError('Mach-O executable header symbol is missing; offsets would be wrong')
    return sorted((address-base,name) for address,name in symbols if address >= base)


def main():
    result = subprocess.run(['xcrun','nm','-nU',sys.argv[1]],check=True,capture_output=True,text=True)
    symbols = parse_nm(result.stdout)
    if len(symbols) < 10: raise ValueError('Executable lacks enough text symbols for diagnostics')
    Path(sys.argv[2]).write_text('# image-relative offset\tMach-O symbol\n'+
                               ''.join(f'{address:x}\t{name}\n' for address,name in symbols))
    print(f'iOS symbol map: {len(symbols)} text symbols')


if __name__ == '__main__': main()
