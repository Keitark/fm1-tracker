"""Offline comparison of selected executed P33 operations; no device access.

This is a byte/call-site regression, not a CPU or electrical simulation.
The LRC/config=0 path is established by manual control-flow review in the report.
Dynamic timing values, IRQ delivery and transport completion are not modeled.
"""
import struct
import unittest

from test_startup_boundary_evidence import ROOT, XIP, pinned
from compare_stock_images import decode_application


# Ordered immediate-argument operations on the reviewed LRC/config=0 path.
# CPU addresses in stock010 and the failed pre-OS NES image, respectively.
OPS = (
    (0x2004832, 0x200100e, 'tx', (0x30, 8)),
    (0x200483c, 0x2001018, 'tx', (0x72, 0xbf)),
    (0x200487e, 0x2001060, 'tx', (0x31, 0x44)),
    (0x2004888, 0x200106a, 'tx', (0x54, 0x44)),
    (0x2004892, 0x2001074, 'tx', (0x13, 0xd1)),
    (0x200489c, 0x200107e, 'tx', (0x14, 0xd1)),
    (0x20048fc, 0x20010de, 'tx', (0x9b, 0)),
    (0x2004906, 0x20010e8, 'tx', (0x32, 3)),
    (0x2004964, 0x2001150, 'tx', (0x74, 0xc0)),
    (0x200496e, 0x200115a, 'tx', (0x90, 0)),
    (0x2004978, 0x2001164, 'tx', (0x92, 0xff)),
    (0x2004982, 0x200116e, 'and', (0x17, 0xfb)),
    (0x200498c, 0x2001178, 'or', (0x11, 0x40)),
    (0x2004996, 0x2001182, 'set', (0x11, 3, 3, 7)),
    (0x20049b4, 0x200119c, 'or', (0x11, 1)),
    (0x20049be, 0x20011a6, 'or', (0x33, 0x80)),
    (0x20049d2, 0x20011b6, 'set', (0, 2, 1, 1)),
    (0x20049ec, 0x20011d0, 'set', (0, 3, 1, 1)),
    (0x2004a06, 0x20011ea, 'set', (0, 2, 1, 0)),
    (0x2004a20, 0x2001204, 'tx', (1, 0x20)),
    (0x2004a2a, 0x200120e, 'tx', (5, 0x40)),
    (0x2004a34, 0x2001218, 'tx', (6, 0x0c)),
    (0x2004a3e, 0x2001222, 'tx', (7, 0xa5)),
    (0x2004a48, 0x200122c, 'tx', (8, 0x96)),
    (0x2004a66, 0x200124a, 'set', (5, 0, 3, 4)),
    (0x2004a74, 0x2001258, 'set', (6, 0, 3, 7)),
    (0x2004a82, 0x2001266, 'set', (5, 4, 2, 3)),
    (0x2004aa8, 0x2001274, 'or', (0, 0x80)),
    (0x2004ab2, 0x200127e, 'set', (0x16, 0, 6, 0)),
    (0x2004ac0, 0x200128c, 'or', (2, 0x40)),
    (0x2004aca, 0x2001296, 'and', (0x17, 0xdf)),
    (0x2004ad4, 0x20012a0, 'set', (9, 0, 4, 15)),
    (0x2004ae6, 0x20012b2, 'tx', (0x58, 0x50)),
    (0x2004af0, 0x20012bc, 'tx', (0x58, 0x5d)),
    (0x2004b10, 0x20012dc, 'tx', (0x59, 0x50)),
    (0x2004b1a, 0x20012e6, 'tx', (0x59, 0x59)),
)


def immediate(reg, value):
    if not (0 <= reg <= 3 and 0 <= value <= 255):
        raise ValueError('Outside reviewed compact immediate encoding')
    return bytes((0x40 | reg | ((value >> 5) << 3), 0x20 | (value & 31)))


class PowerSequenceEvidence(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.old = pinned(ROOT/'private-backups/readback-20260926-024637/analysis/app-FM-1_010.bin',
            'fca75af35d6c3f7914ec0e2ecd4bc025ee904ef58c04c89fc13d4cb4cc113543')
        cls.stock, info, _ = decode_application(pinned(
            ROOT/'private-backups/stock-polled-write-20260928-070755/final-1.bin',
            '444b5d8cea8a3ffa13d72e331e148a34e9f5af902965eb38f80d428dbad2b792'))
        if not info['data_crc_valid']: raise ValueError('Stock CRC')
        cls.nes = pinned(ROOT/'firmware/nes/build/boot-pre-os-smb1/fm1-nes-boot-candidate.app.bin',
            '0b08d4508208b971f332c951147b5ddaed028979908ddd83dce160992be48d24')

    def at(self, blob, address, size):
        offset = address-XIP
        self.assertGreaterEqual(offset, 0)
        self.assertLessEqual(offset+size, len(blob))
        return blob[offset:offset+size]

    def call(self, blob, address, target):
        raw = self.at(blob, address, 6)
        self.assertEqual(raw[:2], b'\x80\xff')
        self.assertEqual(address+6+struct.unpack_from('<i', raw, 2)[0], target)

    def test_36_ordered_immediate_operations_match_both_working_stocks(self):
        helpers = (
            (self.old, 0, dict(tx=0x1c0104e, rx=0x1c0100a, set=0x1c0108c, **{'or':0x1c010b4, 'and':0x1c0244e})),
            (self.stock, 0x260, dict(tx=0x1c0104e, rx=0x1c0100a, set=0x1c0108c, **{'or':0x1c010b4, 'and':0x1c0245c})),
            (self.nes, None, dict(tx=0x1c00f0c, rx=0x1c00ec8, set=0x1c00f4a, **{'or':0x1c00f72, 'and':0x1c00fe8})),
        )
        self.assertEqual(len(OPS), 36)
        for blob, delta, targets in helpers:
            previous = 0
            for old, nes, kind, values in OPS:
                address = nes if delta is None else old+delta
                with self.subTest(address=hex(address), operation=kind, arguments=values):
                    self.assertGreater(address, previous)
                    code = b''.join(immediate(r, v) for r, v in enumerate(values))
                    self.assertEqual(self.at(blob, address, len(code)), code)
                    self.call(blob, address+len(code), targets[kind])
                previous = address

    def test_rtc_reset_address_is_80ab_not_disassembler_nearby_symbol(self):
        for blob, delta in ((self.old, 0), (self.stock, 0x260)):
            self.assertEqual(self.at(blob, 0x2004956+delta, 8), bytes.fromhex('c0ffab8000005120'))
            self.call(blob, 0x200495e+delta, 0x1c010b4)
        self.assertEqual(self.at(self.nes, 0x2001136, 10), bytes.fromhex('c5ffab80000050d6c06e'))
        self.call(self.nes, 0x2001140, 0x1c00ec8)  # extra read, value discarded
        self.assertEqual(self.at(self.nes, 0x2001146, 4), bytes.fromhex('51205016'))
        self.call(self.nes, 0x200114a, 0x1c00f72)

    def test_three_dcdc_delays_have_identical_bounded_loop_bodies(self):
        loop = bytes.fromhex('40e018fc04820000c021f05d')
        for old, nes in ((0x20049e0,0x20011c4),(0x20049fa,0x20011de),(0x2004a14,0x20011f8)):
            self.assertEqual(self.at(self.old, old, 12), loop)
            self.assertEqual(self.at(self.stock, old+0x260, 12), loop)
            self.assertEqual(self.at(self.nes, nes, 12), loop)

    def test_stock_has_three_additional_voltage_readbacks_not_extra_writes(self):
        for blob, delta in ((self.old,0),(self.stock,0x260)):
            for address, reg in ((0x2004a90,6),(0x2004a98,5),(0x2004aa0,5)):
                self.assertEqual(self.at(blob,address+delta,2), immediate(0,reg))
                self.call(blob,address+delta+2,0x1c0100a)
            self.assertEqual(self.at(blob,0x2004aa8+delta,2), immediate(0,0))
        # NES goes directly from the voltage-field call to the next OR operation.
        self.call(self.nes,0x200126e,0x1c00f4a)
        self.assertEqual(self.at(self.nes,0x2001274,2),immediate(0,0))

    def test_p33_byte_transport_and_field_set_bodies_match_after_checked_calls(self):
        def ram(blob, lma, address, size):
            return self.at(blob, lma+address-0x1c00000, size)

        # Resolve calls from the existing byte-checked stock listing and the
        # linked NES listing. Do not use nearest-symbol annotations as operands.
        import re
        def normalized(blob, lma, start, size, listing, targets):
            result = bytearray(ram(blob,lma,start,size))
            seen = []
            for line in listing.splitlines():
                m = re.match(r'\s*([0-9a-f]+):\s+((?:[0-9a-f]{2}\s+)+)\t\s*call (-?(?:0x[0-9a-f]+|[0-9]+))\b',line)
                if not m: continue
                pc = int(m[1],16)
                if not start <= pc < start+size: continue
                code = bytes.fromhex(m[2])
                self.assertEqual(ram(blob,lma,pc,len(code)),code)
                displacement = int(m[3],16 if '0x' in m[3] else 10)
                seen.append(pc+len(code)+displacement)
                result[pc-start:pc-start+len(code)] = bytes(len(code))
            self.assertEqual(seen,targets)
            return bytes(result)

        old_listing = (ROOT/'private-backups/readback-20260926-024637/analysis/ram-copy-linear-sweep.asm').read_text()
        nes_listing = (ROOT/'firmware/nes/build/boot-pre-os-smb1/disassembly.asm').read_text()
        self.assertEqual(ram(self.old,0x20848e0,0x1c00fc8,26),ram(self.nes,0x2029c20,0x1c00e9c,26))
        for old, nes, size, old_calls, nes_calls in (
            (0x1c0100a,0x1c00ec8,68,[0x1c00fa0]+[0x1c00fc8]*3+[0x1c00fe2],
                                      [0x1c00e86]+[0x1c00e9c]*3+[0x1c00eb6]),
            (0x1c0104e,0x1c00f0c,62,[0x1c00fa0]+[0x1c00fc8]*3+[0x1c00fe2],
                                      [0x1c00e86]+[0x1c00e9c]*3+[0x1c00eb6]),
            (0x1c0108c,0x1c00f4a,40,[0x1c0100a,0x1c0104e],[0x1c00ec8,0x1c00f0c])):
            self.assertEqual(normalized(self.old,0x20848e0,old,size,old_listing,old_calls),
                             normalized(self.nes,0x2029c20,nes,size,nes_listing,nes_calls))


if __name__ == '__main__': unittest.main(verbosity=2)
