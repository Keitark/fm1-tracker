"""Offline evidence for the failed CPU1-guard bench image; NOT a boot test.

Reads saved artifacts only. No USB access, helper upload, build or flash.
"""
import struct
import sys
import unittest
from pathlib import Path

from test_stock_startup_comparison import ROOT, STOCK, at, pinned

sys.path.insert(0, str(ROOT / 'firmware/nes'))
from build_stock_image import V14_BACKUP, V14_SHA, verify_candidate

APP = ROOT / 'firmware/nes/build/boot-cpu1-guard-smb1/fm1-nes-boot-candidate.app.bin'
RECEIPT = ROOT / 'private-backups/cpu1-guard-write-20260928-021650'
FULL_SHA = '51dc35fdf0bcbe4e105c4f84e5c4ee52e827f4a88169c81b65541488d5998aa1'


class NoScreenEvidence(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.nes = pinned(APP, '63476ce1f89cd6b83fe4a915396a07668bdfca49696d3917e91aff523508a6ea')
        cls.stock = pinned(STOCK, 'fca75af35d6c3f7914ec0e2ecd4bc025ee904ef58c04c89fc13d4cb4cc113543')

    def n(self, address, size):
        return at(self.nes, address, size, 0x02029c20)

    def s(self, address, size):
        return at(self.stock, address, size, 0x020848e0)

    def call(self, address):
        op = self.n(address, 6)
        if op[:2] == b'\x80\xff':
            return address + 6 + struct.unpack_from('<i', op, 2)[0]
        self.assertEqual(op[:2], b'\xbf\xea')
        return address + 4 + struct.unpack_from('<h', op, 2)[0] * 2

    def test_saved_full_readbacks_container_and_application(self):
        first = pinned(RECEIPT / 'final-1.bin', FULL_SHA)
        second = pinned(RECEIPT / 'final-2.bin', FULL_SHA)
        self.assertEqual(first, second)
        report = verify_candidate(pinned(V14_BACKUP, V14_SHA), first, self.nes)
        self.assertEqual(report['candidate_bytes'], 1048576)
        self.assertTrue(all(report['checks'].values()))

    def test_early_registry_and_picture_dependency_order(self):
        self.assertEqual(struct.unpack('<4I', self.n(0x02029b7c, 16)),
                         (0x02001cf6, 0x020018dc, 0x020290bc, 0x020096aa))
        self.assertEqual(struct.unpack('<I', self.n(0x02029b90, 4))[0], 0x02006462)
        self.assertEqual(self.call(0x02009720), 0x02009434)  # sdfile -> NOR open
        self.assertEqual(self.call(0x02001b72), 0x0200162c)  # power
        self.assertEqual(self.call(0x02001b7c), 0x0200180c)  # first picture
        self.assertEqual(struct.unpack('<I', self.n(0x02029b98, 4))[0], 0x02006c08)

    def test_nor_open_still_enters_ram_flash_routines_before_picture(self):
        self.assertEqual(self.call(0x0200948c), 0x01c00aea)  # ID/UID read
        self.assertEqual(self.call(0x020094a2), 0x01c00758)  # SR snapshot
        self.assertEqual(self.call(0x01c00356), 0x01c01d8a)  # cross-core critical

    def test_flash_control_default_is_same_not_a_missing_enable_bit(self):
        # Ignore misleading nearest-symbol annotation '<... : 1028>'.
        # Instruction operand is decimal 4137 = 0x1029, bytes match stock.
        self.assertEqual(self.s(0x01c0006a, 4), bytes.fromhex('41 e0 29 10'))
        self.assertEqual(self.n(0x01c00392, 4), self.s(0x01c0006a, 4))

    def test_protect_init_reads_status_without_program_call(self):
        self.assertEqual([self.call(a) for a in
                          (0x01c0075a, 0x01c0075e, 0x01c00764,
                           0x01c0077a, 0x01c00780)],
                         [0x01c00330, 0x01c00492, 0x01c005ce,
                          0x01c00540, 0x01c0040a])
        self.assertEqual(self.n(0x01c005d6, 2), bytes.fromhex('40 25'))  # 05
        self.assertEqual(self.n(0x01c005ee, 2), bytes.fromhex('48 35'))  # 35

    def test_unbounded_wait_sites_exist_not_proof_of_execution(self):
        self.assertEqual(self.n(0x01c01dbe, 6), bytes.fromhex('09 40 01 f8 fc 03'))
        self.assertEqual(self.n(0x01c01de2, 6), bytes.fromhex('71 60 51 e8 fd 71'))
        self.assertEqual(self.n(0x01c01dec, 4), bytes.fromhex('09 40 71 5d'))
        self.assertEqual(self.n(0x01c00376, 6), bytes.fromhex('12 60 82 fd fd 01'))


if __name__ == '__main__':
    unittest.main(verbosity=2)
