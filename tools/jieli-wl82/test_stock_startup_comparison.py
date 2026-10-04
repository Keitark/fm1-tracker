"""Offline regression evidence for the failed screen-first image, NOT a boot test.

No device access, build, download, key access, or firmware changes. Fixed-address
interpretations require the exact stock010 and last-flashed application hashes.
"""
import hashlib
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[2]
STOCK = ROOT / 'private-backups/readback-20260926-024637/analysis/app-FM-1_010.bin'
NES = ROOT / 'firmware/nes/build/boot-screen-first-reviewed-smb1/fm1-nes-boot-candidate.app.bin'
XIP = 0x02000120
RAM = 0x01c00000


def pinned(path, digest):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != digest:
        raise ValueError('Unreviewed image: ' + str(path))
    return data


def at(data, address, size, ram_lma):
    if RAM <= address < 0x01c80000:
        address = ram_lma + address - RAM
    offset = address - XIP
    if offset < 0 or offset + size > len(data):
        raise ValueError('Address outside image')
    return data[offset:offset + size]


class StartupEvidence(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.stock = pinned(STOCK, 'fca75af35d6c3f7914ec0e2ecd4bc025ee904ef58c04c89fc13d4cb4cc113543')
        cls.nes = pinned(NES, 'eda772a719fe94600023d8d709ebbe972aa165386131036b59310769201c4a88')

    def s(self, address, size):
        return at(self.stock, address, size, 0x020848e0)

    def n(self, address, size):
        return at(self.nes, address, size, 0x02029c20)

    def long_target(self, read, address):
        data = read(address, 6)
        self.assertEqual(data[:2], b'\x80\xff')
        return address + 6 + struct.unpack_from('<i', data, 2)[0]

    def test_cache_body_equal_except_three_reviewed_relocations(self):
        stock = bytearray(self.s(0x01c00e88, 0x110))
        nes = bytearray(self.n(0x01c00cc0, 0x110))
        # Source pointer, memmove target, memset target; NOT MMIO operands.
        for offset, opcode in ((0xe8, b'\xc1\xff'),
                               (0xf4, b'\x80\xff'),
                               (0x108, b'\x80\xff')):
            self.assertEqual(stock[offset:offset+2], opcode)
            self.assertEqual(nes[offset:offset+2], opcode)
            stock[offset+2:offset+6] = bytes(4)
            nes[offset+2:offset+6] = bytes(4)
        self.assertEqual(stock, nes)

    def test_stock_launch_saves_sets_and_restores_sys_div(self):
        self.assertEqual(self.s(0x0205a2c8, 14), bytes.fromhex(
            'c0 ff 08 00 01 00 01 60 81 20 00 ef 08 00'))
        self.assertEqual(self.s(0x0205a2ee, 12), bytes.fromhex(
            '1a 40 72 5e 01 20 81 60 02 81 80 00'))
        self.assertEqual(self.long_target(self.s, 0x02002040), 0x0205a2b8)
        self.assertEqual(self.long_target(self.s, 0x02002046), 0x0205a2fa)
        self.assertEqual(self.long_target(self.s, 0x02002070), 0x0205a8a6)

    def test_nes_launch_waits_without_stock_divider_sequence(self):
        launch = self.n(0x02027002, 0x2a)
        self.assertEqual(launch, bytes.fromhex(
            'c0 ff f8 ff c7 01 c1 ff 24 02 00 02 81 60 '
            'c0 ff 2d aa c1 01 41 20 89 40 '
            'c1 ff 04 e0 ee 01 00 ef 08 10 c0 ef 02 10 09 40 71 5e'))
        self.assertEqual(self.long_target(self.n, 0x02000d9c), 0x02026f16)

    def test_cpu1_destinations_and_nes_scheduler_call(self):
        self.assertEqual(self.s(0x020001c4, 8), bytes.fromhex(
            'e0 ff ae 23 c0 01 81 00'))  # stock -> internal RAM
        self.assertEqual(self.n(0x02000230, 8), bytes.fromhex(
            'e0 ff 6c 08 00 02 81 00'))  # NES -> XIP
        self.assertEqual(self.long_target(self.n, 0x02000886), 0x02026f16)
        self.assertEqual(self.s(0x01c023ae, 10), bytes.fromhex(
            'c0 ff e8 fe c1 01 41 21 89 40'))
        self.assertEqual(self.n(0x0200086e, 10), bytes.fromhex(
            'c0 ff 2d aa c1 01 41 21 89 40'))

    def test_nes_idle_task_enables_interrupts(self):
        self.assertEqual(self.long_target(self.n, 0x02028e8a), 0x01c00e46)
        self.assertEqual(self.n(0x01c00e5e, 2), bytes.fromhex('61 00'))

    def test_first_picture_is_in_app_task_not_pre_os(self):
        # Direct short call: address + four-byte instruction + signed imm16*2.
        instruction = self.n(0x02001b7c, 4)
        self.assertEqual(instruction, bytes.fromhex('bf ea 46 fe'))
        target = 0x02001b80 + struct.unpack_from('<h', instruction, 2)[0]*2
        self.assertEqual(target, 0x0200180c)
        # app task handler is supplied to task_create in main before os_start.
        self.assertEqual(self.n(0x02000d8c, 6), bytes.fromhex('c0 ff 36 1b 00 02'))


if __name__ == '__main__':
    unittest.main(verbosity=2)
