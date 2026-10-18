# SPDX-License-Identifier: GPL-3.0-only
# Reused from Keitark/fm1-mdx at 4141057; see licenses/fm1-mdx-GPL-3.0.txt.
from pathlib import Path
import os
import subprocess
import unittest
EXE=Path(os.environ.get('FM1_UAC_TEST_EXE',Path(__file__).resolve().parents[2]/'build/host-usb/Release/uac_tests.exe'))
class ProfileTests(unittest.TestCase):
    def test_actual_c_descriptor_tree(self):
        b=bytes.fromhex(subprocess.check_output([str(EXE),'--descriptor'],text=True).strip())
        entries=[];i=0
        while i<len(b):
            n=b[i];self.assertGreaterEqual(n,2);self.assertLessEqual(i+n,len(b));entries.append(b[i:i+n]);i+=n
        self.assertEqual(i,173)
        self.assertEqual(list(entries[0]),[8,11,2,3,1,1,0,0])
        interfaces=[x for x in entries if x[1]==4]
        self.assertEqual([(x[2],x[3],x[4]) for x in interfaces],[(2,0,0),(3,0,0),(3,1,1),(4,0,0),(4,1,1)])
        header=entries[2];self.assertEqual(header[7:],bytes([2,3,4]));self.assertEqual(int.from_bytes(header[5:7],'little'),sum(map(len,entries[2:7])))
        endpoints=[x for x in entries if x[1]==5]
        self.assertEqual([x[2] for x in endpoints],[1,0x81])
        self.assertEqual([x[3] for x in endpoints],[9,13])
        for x in endpoints:self.assertEqual(int.from_bytes(x[4:6],'little'),192);self.assertEqual(x[6:],bytes([1,0,0]))
        formats=[x for x in entries if x[1:3]==bytes([36,2]) and len(x)==11]
        self.assertEqual(len(formats),2)
        for x in formats:self.assertEqual(x[3:8],bytes([1,2,2,16,1]));self.assertEqual(int.from_bytes(x[8:11],'little'),48000)
        terminals=[x for x in entries if x[1]==36 and x[2] in (2,3) and len(x) in (9,12)]
        self.assertEqual([x[3] for x in terminals],[1,2,3,4]);self.assertEqual(terminals[1][7],1);self.assertEqual(terminals[3][7],3)
        self.assertEqual(int.from_bytes(terminals[2][4:6],'little'),0x0603) # virtual line input for Windows recording clients
if __name__=='__main__':unittest.main()
