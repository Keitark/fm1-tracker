"""Offline evidence for accepted stock polling vs failed pre-OS NES startup.

Reads saved binaries only. No device access, firmware modification or build.
These checks establish static structure, NOT execution on hardware.
"""
import hashlib
from pathlib import Path
import struct
import unittest

from compare_stock_images import decode_application

ROOT = Path(__file__).resolve().parents[2]
XIP, RAM = 0x02000120, 0x01c00000


def pinned(path, digest):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != digest:
        raise ValueError('Unreviewed evidence file: '+str(path))
    return data


class StartupBoundary(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.stock_raw = pinned(ROOT/'private-backups/stock-polled-write-20260928-070755/final-1.bin',
            '444b5d8cea8a3ffa13d72e331e148a34e9f5af902965eb38f80d428dbad2b792')
        cls.nes_raw = pinned(ROOT/'private-backups/pre-os-write-20260928-025138/final-1.bin',
            '8da9ba09455e10f9cd04666df10a32d7925aaf21efee9f6640631f39dba0d5ca')
        cls.stock, cls.stock_info, _ = decode_application(cls.stock_raw)
        cls.nes, cls.nes_info, _ = decode_application(cls.nes_raw)

    def at(self, app, address, size, lma):
        if RAM <= address < 0x01c80000:
            address = lma + address-RAM
        offset = address-XIP
        self.assertGreaterEqual(offset, 0)
        self.assertLessEqual(offset+size, len(app))
        return app[offset:offset+size]

    def n(self, address, size): return self.at(self.nes, address, size, 0x02029c20)
    def s(self, address, size): return self.at(self.stock, address, size, 0x02084f60)

    def call(self, read, address, size):
        code = read(address, size)
        if size == 6:
            self.assertEqual(code[:2], b'\x80\xff')
            return address+6+struct.unpack_from('<i', code, 2)[0]
        self.assertEqual(code[:2], b'\xbf\xea')
        return address+4+struct.unpack_from('<h', code, 2)[0]*2

    def test_saved_applications_and_unchanged_boot_region(self):
        self.assertTrue(self.stock_info['data_crc_valid'])
        self.assertTrue(self.nes_info['data_crc_valid'])
        self.assertEqual(self.stock_raw[:0x4000], self.nes_raw[:0x4000])
        self.assertEqual(hashlib.sha256(self.stock).hexdigest(),
            '1c1ecc0b62bc2ee922dcdc7fe48e1bb6f09a3f5fea330a34c9f536eff540cd82')
        self.assertEqual(hashlib.sha256(self.nes).hexdigest(),
            '0b08d4508208b971f332c951147b5ddaed028979908ddd83dce160992be48d24')

    def test_executed_memory_operands(self):
        for read, entry, sp, ssp, bss, bss_size, lma, data_size in (
            (self.s, 0x02000124, 0x01c15d14, 0x01c16d14, 0x01c09e9c, 0x184c0, 0x02084f60, 0x9e9c),
            (self.n, 0x0200018a, 0x01c18658, 0x01c19658, 0x01c02870, 0x181c0, 0x02029c20, 0x2870)):
            for offset, opcode, value in ((0, 'eeff', sp), (6, 'edff', ssp),
                (18, 'c3ff', bss), (26, 'c2ff', bss_size), (40, 'c4ff', RAM),
                (46, 'c1ff', lma), (52, 'c2ff', data_size)):
                self.assertEqual(read(entry+offset, 6), bytes.fromhex(opcode)+struct.pack('<I', value))
            self.assertEqual(bss, RAM+data_size)
            self.assertEqual((bss_size | data_size) & 3, 0)
            self.assertLess(bss, sp)
            self.assertLess(ssp, bss+bss_size)
            self.assertLess(bss+bss_size, 0x01c7fd4c)
            self.assertEqual(len(read(lma, data_size)), data_size)

    def test_cache_body_equivalent_after_only_address_relocations(self):
        stock, nes = bytearray(self.s(0x01c00e8c, 272)), bytearray(self.n(0x01c00cc0, 272))
        for offset, opcode in ((0xe8, b'\xc1\xff'), (0xf4, b'\x80\xff'), (0x108, b'\x80\xff')):
            self.assertEqual(stock[offset:offset+2], opcode)
            self.assertEqual(nes[offset:offset+2], opcode)
            stock[offset+2:offset+6] = bytes(4)
            nes[offset+2:offset+6] = bytes(4)
        self.assertEqual(stock, nes)

    def test_initialized_software_lock_state_is_zero(self):
        for address, size in ((0x01c025e0, 1), (0x01c025e4, 8), (0x01c025ec, 1),
                              (0x01c0280c, 8), (0x01c02814, 8), (0x01c0282c, 4)):
            self.assertEqual(self.n(address, size), bytes(size))
        # This tests bytes in the copied RAM payload, not live hardware locks.

    def test_pre_display_still_contains_mutex_initialization(self):
        self.assertEqual(self.call(self.n, 0x02001828, 4), 0x02000b06)
        self.assertEqual(self.call(self.n, 0x02000b0e, 6), 0x02027d84)
        self.assertEqual(self.call(self.n, 0x02027d96, 4), 0x020270ac)
        self.assertEqual(self.n(0x020270f6, 2), bytes.fromhex('41 00'))
        self.assertEqual(self.n(0x0202713e, 2), bytes.fromhex('40 00'))
        self.assertEqual(self.call(self.n, 0x0200182c, 4), 0x02000b16)
        self.assertEqual(self.call(self.n, 0x02001830, 4), 0x02001686)
        self.assertEqual(self.n(0x02001686, 16), bytes.fromhex(
            '10 04 bf ea b2 fe 51 81 80 ff 2e 81 02 00 00 04'))
        self.assertEqual(self.call(self.n, 0x02001844, 6), 0x02026f16)

    def test_early_p33_waits_and_boot_info_extra_write(self):
        self.assertEqual(self.n(0x01c00e92, 6), bytes.fromhex('b0 00 40 e8 fd ff'))
        self.assertEqual(self.n(0x01c00eaa, 6), bytes.fromhex('10 60 50 e8 fd 0b'))
        self.assertEqual(self.n(0x020002dc, 10), bytes.fromhex(
            '48 66 c1 ff 0c ff c7 01 90 60'))
        self.assertEqual(self.call(self.n, 0x02000196, 6), 0x02002ac2)
        self.assertEqual(self.call(self.n, 0x02002aee, 6), 0x02000292)
        self.assertEqual(self.call(self.s, 0x02000130, 6), 0x020001e6)


if __name__ == '__main__': unittest.main(verbosity=2)
