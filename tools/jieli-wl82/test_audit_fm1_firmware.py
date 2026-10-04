import struct
import unittest

from audit_fm1_firmware import BASE, find_string_tables, make_elf
from compare_stock_images import ROOT, decode_application


class AuditTests(unittest.TestCase):
    def test_analysis_elf_preserves_payload_and_address(self):
        payload = bytes(range(256)) * 3
        elf = make_elf(payload, BASE)
        self.assertEqual(elf[:4], b'\x7fELF')
        self.assertEqual(struct.unpack_from('<H', elf, 18)[0], 0xf1)
        self.assertEqual(struct.unpack_from('<I', elf, 24)[0], BASE)
        self.assertEqual(elf[0x100:0x100 + len(payload)], payload)
        self.assertEqual(struct.unpack_from('<I', elf, 60)[0], BASE)

    def test_pointer_table_requires_valid_strings(self):
        data = bytearray(64)
        for i in range(4):
            struct.pack_into('<I', data, i * 4, BASE + 32 + i * 5)
            data[32 + i * 5:37 + i * 5] = b'Test\0'
        tables = find_string_tables(data)
        self.assertEqual(len(tables), 1)
        self.assertEqual(len(tables[0]['entries']), 4)
        self.assertEqual(find_string_tables(bytes(64)), [])

    def test_actual_ui_table_and_corrected_mapping(self):
        backup = ROOT / 'private-backups/readback-20260926-024637/fm1-flash-1MiB.bin'
        if not backup.exists():
            self.skipTest('Private backup unavailable')
        app, info, layout = decode_application(backup.read_bytes())
        self.assertTrue(info['data_crc_valid'])
        self.assertEqual(int(layout['entry_point'], 16), BASE)
        self.assertEqual(app[:4], b'\x04\x81\x80\x00')
        ptr = struct.unpack_from('<I', app, 0x4f9ec)[0]
        self.assertEqual(app[ptr - BASE:ptr - BASE + 10], b'Algorithm\0')
        self.assertTrue(any('Algorithm' in [e['text'] for e in t['entries']]
                            for t in find_string_tables(app)))


if __name__ == '__main__':
    unittest.main()
