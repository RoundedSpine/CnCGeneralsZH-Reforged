#!/usr/bin/env python3
"""Validate LAN packaging requests without claiming an Apple permission grant."""
from pathlib import Path
import plistlib
import subprocess
import sys
import tempfile
import unittest

CODE = Path(__file__).resolve().parents[1]
class IOSNetworkPermissions(unittest.TestCase):
    def test_privacy_message_and_opt_in_entitlements(self):
        with open(CODE/'Platform/iOS/Info.plist.in','rb') as f: info=plistlib.load(f)
        self.assertTrue(info['NSLocalNetworkUsageDescription'].strip())
        template=(CODE/'Platform/iOS/Reforged.entitlements').read_bytes()
        self.assertNotIn('com.apple.developer.networking.multicast',plistlib.loads(template))
        script=(CODE/'Tools/make-ios-ipa.sh').read_text()
        block=script.split('# IOS_NETWORK_ENTITLEMENTS_BEGIN\n',1)[1].split('# IOS_NETWORK_ENTITLEMENTS_END',1)[0]
        python=block.split("<<'PY'\n",1)[1].rsplit('\nPY',1)[0]
        for flag in ('0','1','invalid'):
            with self.subTest(flag=flag), tempfile.TemporaryDirectory() as folder:
                target=Path(folder)/'entitlements.plist';target.write_bytes(template)
                r=subprocess.run([sys.executable,'-c',python,str(target),flag],capture_output=True,text=True)
                if flag=='invalid':
                    self.assertNotEqual(r.returncode,0)
                    self.assertEqual(target.read_bytes(),template)
                    continue
                self.assertEqual(r.returncode,0,r.stderr)
                actual=plistlib.loads(target.read_bytes())
                self.assertIs(actual['com.apple.developer.kernel.increased-memory-limit'],True)
                self.assertIs(actual['com.apple.developer.kernel.extended-virtual-addressing'],True)
                self.assertEqual(actual.get('com.apple.developer.networking.multicast',False),flag=='1')

if __name__ == '__main__': unittest.main()
