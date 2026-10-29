"""Tracker AV linked-image audits. All mutations stay in memory; no device I/O.

Use --build PATH --require-actual for the final candidate. Normal unittest
discovery skips this class when the linked artifacts are absent.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'firmware/nes'))
from audit_boot import audit, PARTS
from audit_power import call_target

BUILD = ROOT / 'firmware/usb-diag/build/tracker-workflow20-av'
REQUIRE_ACTUAL = False


class Elf32:
    """Read only the ELF32 sections and symbols needed for bounded mutations."""
    def __init__(self, elf):
        if len(elf) < 52 or elf[:7] != b'\x7fELF\x01\x01\x01':
            raise ValueError('Expected ELF32 little endian test fixture')
        section_offset = struct.unpack_from('<I', elf, 32)[0]
        entry_size, count, string_index = struct.unpack_from('<HHH', elf, 46)
        if entry_size != 40 or not count or string_index >= count or section_offset + count * 40 > len(elf):
            raise ValueError('Invalid ELF section table in test fixture')
        raw = [struct.unpack_from('<10I', elf, section_offset + i * 40) for i in range(count)]

        def data(section):
            offset, size = section[4:6]
            if offset + size > len(elf):
                raise ValueError('ELF test fixture section extends beyond file')
            return elf[offset:offset + size]

        def string(table, offset):
            end = table.find(b'\0', offset)
            if not 0 <= offset < len(table) or end < 0:
                raise ValueError('Invalid ELF test fixture string')
            return table[offset:end].decode('utf-8')

        names = data(raw[string_index])
        self.sections = {string(names, section[0]): section for section in raw}
        table = self.sections['.symtab']
        if table[9] != 16 or table[5] % 16 or table[6] >= count:
            raise ValueError('Invalid ELF symbol table in test fixture')
        strings = raw[table[6]]
        text = data(strings)
        entries = data(table)
        self.symbols = {}
        self.symbol_entries = {}
        self.symbol_names = {}
        for offset in range(0, len(entries), 16):
            name_offset, value, size, _, _, index = struct.unpack_from('<IIIBBH', entries, offset)
            name = string(text, name_offset)
            if name:
                self.symbols[name] = (value, size, index)
                self.symbol_entries[name] = table[4] + offset
                self.symbol_names[name] = strings[4] + name_offset

    def file_offset(self, address, size=1):
        for section in self.sections.values():
            if section[2] & 2 and section[1] != 8 and section[3] <= address and address + size <= section[3] + section[5]:
                return section[4] + address - section[3]
        raise ValueError('Mutation address is outside allocated ELF file data')


class TrackerAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        required = ('build-manifest.json', 'fm1-usb-diag.elf', 'fm1-usb-diag.app.bin')
        missing = [name for name in required if not (BUILD / name).is_file()]
        if missing:
            reason = 'Actual tracker AV candidate artifacts are absent: ' + ', '.join(missing)
            if REQUIRE_ACTUAL:
                raise ValueError(reason)
            raise unittest.SkipTest(reason)
        cls.manifest = json.loads((BUILD / 'build-manifest.json').read_text(encoding='utf-8'))
        cls.elf = (BUILD / 'fm1-usb-diag.elf').read_bytes()
        cls.app = (BUILD / 'fm1-usb-diag.app.bin').read_bytes()
        cls.image = Elf32(cls.elf)
        cls.sections = cls.image.sections
        cls.symbols = cls.image.symbols

    def check(self, elf=None, **flags):
        elf = self.elf if elf is None else elf
        sections = Elf32(elf).sections
        app = b''.join(elf[sections[name][4]:sections[name][4] + sections[name][5]] for name in PARTS)
        options = dict(usb_only=True, usb_peripheral_tests=True, usb_mod=True,
                       usb_audio=True, usb_video=True,
                       require_boot_trace=True, require_board_power=True)
        options.update(flags)
        return audit(elf, app, **options)

    def corrupt(self, symbol, delta=0):
        address, size, _ = self.symbols[symbol]
        self.assertTrue(0 <= delta < size, 'Mutation exceeds symbol: ' + symbol)
        elf = bytearray(self.elf)
        elf[self.image.file_offset(address + delta)] ^= 4
        return bytes(elf)

    def corrupt_symbol(self, name, field, value):
        self.assertIn(field, ('value', 'size'))
        elf = bytearray(self.elf)
        struct.pack_into('<I', elf, self.image.symbol_entries[name] + (4 if field == 'value' else 8), value)
        return bytes(elf)

    def call_offset(self, caller, callee):
        start, size, _ = self.symbols[caller]
        offset = self.image.file_offset(start, size)
        body = self.elf[offset:offset + size]
        hits = []
        for off in range(0, size - 1, 2):
            length = (6 if body[off:off + 2] == b'\x80\xff' else
                      4 if body[off] & 0xc0 == 0x80 and body[off + 1] == 0xea else
                      2 if body[off] & 0x8f == 1 and body[off + 1] & 0xe0 == 0x80 else 0)
            if length and off + length <= size and call_target(body[off:off + length], start + off) == self.symbols[callee][0]:
                hits.append(off)
        self.assertEqual(len(hits), 1, 'Expected one call: ' + caller + ' -> ' + callee)
        return hits[0]

    def test_actual_candidate_is_audited_standalone_mod_with_exact_app(self):
        self.assertEqual(self.manifest.get('status'), 'LINKED_USB_DIAGNOSTIC_UNTESTED')
        self.assertEqual(self.manifest.get('static_audit', {}).get('static_audit'), 'passed')
        for name in ('mod', 'peripheral_tests', 'usb_audio', 'usb_video', 'mod_usb_audio', 'mod_uvc'):
            self.assertIs(self.manifest.get(name), True, 'Missing explicit boolean ' + name)
        for name in ('factory_diag', 'nes_smb1', 'mdx'):
            self.assertIs(self.manifest.get(name), False, 'Unexpected hybrid profile ' + name)
        self.assertEqual(len(self.app), self.manifest['application_bytes'])
        self.assertEqual(hashlib.sha256(self.app).hexdigest(), self.manifest['application_sha256'])
        report = self.check()
        self.assertEqual(report['static_audit'], 'passed')
        self.assertEqual(report['application_bytes'], len(self.app))
        self.assertEqual(report['application_sha256'], self.manifest['application_sha256'])

    def test_peripheral_task_registration_is_pinned(self):
        needle = b'#C0peripheral\0'
        offset = self.elf.find(needle)
        self.assertGreaterEqual(offset, 0, 'Peripheral task name not found')
        elf = bytearray(self.elf)
        elf[offset] ^= 4
        with self.assertRaisesRegex(ValueError, 'task registration'):
            self.check(bytes(elf))

    def test_cache_bss_startup_clear_is_pinned(self):
        with self.assertRaisesRegex(ValueError, 'cache BSS startup clear'):
            self.check(self.corrupt('cache_way_config', 0x106))

    def test_polling_bulk_writer_is_rejected(self):
        old_name, new_name = 'fm1_tracker_speaker_fold', 'usb_g_bulk_write'
        self.assertLessEqual(len(new_name), len(old_name))
        offset = self.image.symbol_names[old_name]
        elf = bytearray(self.elf)
        elf[offset:offset + len(new_name) + 1] = new_name.encode() + b'\0'
        self.assertEqual(len(elf), len(self.elf))
        with self.assertRaisesRegex(ValueError, 'polling writer'):
            self.check(bytes(elf))

    def test_usb_packet_writer_instructions_are_pinned(self):
        with self.assertRaisesRegex(ValueError, 'USB packet'):
            self.check(self.corrupt('fm1_usb_packet_write', 0x76))

    def test_audio_dma_size_is_pinned(self):
        with self.assertRaisesRegex(ValueError, 'Audio DMA'):
            self.check(self.corrupt_symbol('fm1_uac_dma', 'size', 511))

    def test_uac_descriptor_is_pinned(self):
        with self.assertRaisesRegex(ValueError, 'UAC1'):
            self.check(self.corrupt('fm1_uac_descriptor'))

    def test_tracker_uvc_dma_size_is_pinned(self):
        with self.assertRaisesRegex(ValueError, 'Tracker UVC DMA'):
            self.check(self.corrupt_symbol('fm1_uvc_dma', 'size', 1023))

    def test_tracker_uvc_descriptor_is_pinned(self):
        with self.assertRaisesRegex(ValueError, 'Tracker UVC descriptor'):
            self.check(self.corrupt('fm1_uvc_video_descriptor', 90))

    def test_tracker_uvc_setup_policy_is_pinned(self):
        with self.assertRaisesRegex(ValueError, 'Tracker UVC setup policy'):
            self.check(self.corrupt('filter', 0x38))

    def test_tracker_lcd_conversion_instructions_are_pinned(self):
        with self.assertRaisesRegex(ValueError, 'Tracker LCD copy/conversion instructions'):
            self.check(self.corrupt('fm1_tracker_lcd_submit', 0x18))

    def test_tracker_lcd_conversion_pointer_is_pinned(self):
        with self.assertRaisesRegex(ValueError, 'Tracker LCD copy/conversion pointer'):
            self.check(self.corrupt_symbol('row_pixels', 'value', self.sections['.ram0_bss'][3]))

    def test_tracker_uvc_initializes_before_usb_attachment(self):
        offset = self.call_offset('app_task_handler', 'fm1_uvc_setup_init')
        with self.assertRaisesRegex(ValueError, 'Tracker UVC must initialize'):
            self.check(self.corrupt('app_task_handler', offset))

    def test_tracker_uvc_producer_follows_lcd_copy(self):
        offset = self.call_offset('fm1_peripheral_task', 'fm1_tracker_capture_step')
        with self.assertRaisesRegex(ValueError, 'Tracker UVC producer'):
            self.check(self.corrupt('fm1_peripheral_task', offset))

    def test_tracker_uvc_stops_on_confirmed_uboot(self):
        offset = self.call_offset('fm1_peripheral_usb_audio_quiesce', 'fm1_uvc_capture_stop')
        with self.assertRaisesRegex(ValueError, 'Tracker UVC confirmed'):
            self.check(self.corrupt('fm1_peripheral_usb_audio_quiesce', offset))

    def test_tracker_dma_quiesces_before_uboot_entry(self):
        offset = self.call_offset('fm1_usb_rx_irq', 'fm1_peripheral_usb_audio_quiesce')
        with self.assertRaisesRegex(ValueError, 'tracker UBOOT DMA teardown'):
            self.check(self.corrupt('fm1_usb_rx_irq', offset))

    def test_tracker_usb_stereo_capture_precedes_speaker_fold(self):
        caller = 'audio_output'
        first, second = 'fm1_usb_audio_dac', 'fm1_tracker_speaker_fold'
        offsets = (self.call_offset(caller, first), self.call_offset(caller, second))
        elf = bytearray(self.elf)
        for offset, target in zip(offsets, (second, first)):
            pc = self.symbols[caller][0] + offset
            position = self.image.file_offset(pc, 4)
            code = self.elf[position:position + 4]
            self.assertTrue(code[0] & 0xc0 == 0x80 and code[1] == 0xea,
                            'Speaker capture swap requires reviewed four-byte calls')
            distance = self.symbols[target][0] - (pc + 4)
            self.assertEqual(distance % 2, 0)
            displacement = distance // 2
            self.assertTrue(-(1 << 21) <= displacement < (1 << 21))
            encoded = bytes((0x80 | ((displacement >> 16) & 63), 0xea,
                             displacement & 255, (displacement >> 8) & 255))
            self.assertEqual(call_target(encoded, pc), self.symbols[target][0])
            elf[position:position + 4] = encoded
        with self.assertRaisesRegex(ValueError, 'Tracker USB stereo capture'):
            self.check(bytes(elf))

    def test_tracker_speaker_average_instructions_are_pinned(self):
        with self.assertRaisesRegex(ValueError, '64-bit speaker average'):
            self.check(self.corrupt('fm1_tracker_speaker_fold', 0x10))

    def test_tracker_analog_startup_gain_instructions_are_pinned(self):
        with self.assertRaisesRegex(ValueError, 'Tracker analog startup gain instructions'):
            self.check(self.corrupt('fm1_tracker_analog_gain', 0x54))

    def test_tracker_edit_history_size_is_pinned(self):
        with self.assertRaisesRegex(ValueError, 'Tracker edit history'):
            self.check(self.corrupt_symbol('tracker_history', 'size', 10319))

    def test_linked_tracker_video_cannot_be_hidden(self):
        with self.assertRaises(ValueError):
            self.check(usb_video=False)

    def test_linked_tracker_requires_mod_profile(self):
        with self.assertRaises(ValueError):
            self.check(usb_mod=False)

    def test_mod_cannot_be_a_factory_profile(self):
        with self.assertRaises(ValueError):
            self.check(factory_diag=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--build', type=Path, default=BUILD)
    parser.add_argument('--require-actual', action='store_true')
    options, remaining = parser.parse_known_args()
    BUILD = options.build.resolve()
    REQUIRE_ACTUAL = options.require_actual
    unittest.main(argv=[sys.argv[0], *remaining])
