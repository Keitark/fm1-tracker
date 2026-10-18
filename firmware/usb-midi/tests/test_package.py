# SPDX-License-Identifier: Apache-2.0
"""Synthetic format and preservation tests; no private images or device I/O."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('fm1_midi_package', Path(__file__).parents[1] / 'package.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.base = b'\xff' * MODULE.FLASH_BYTES
        changed = bytearray(self.base)
        changed[0x4000] = 0x24
        changed[MODULE.APP_START:MODULE.APP_START + 3] = b'new'
        self.changed = bytes(changed)
        self.loader = bytes(range(256)) * 4
        self.loader_digest = hashlib.sha256(self.loader).hexdigest()

    def make(self, candidate=None, **changes):
        candidate = self.changed if candidate is None else candidate
        options = dict(baseline_sha256=MODULE.sha256(self.base),
                       candidate_sha256=MODULE.sha256(candidate), target_build_sha256='ab' * 32)
        options.update(changes)
        return MODULE.make_package(self.base, candidate, self.loader, **options)

    def test_independent_container_fields_and_roundtrip(self):
        with patch.object(MODULE, 'LOADER_SHA256', self.loader_digest):
            package, manifest = self.make()
        image = b''.join(package[i * 48:i * 48 + 47] for i in range(20)) + package[960:]
        self.assertEqual(MODULE.sha256(image), manifest['imageSha256'])
        self.assertEqual(len(image) + 20, len(package))
        head = MODULE.header_codec(image[:64])
        self.assertEqual(MODULE.crc16(head[2:]), struct.unpack_from('<H', head)[0])
        self.assertEqual(struct.unpack_from('<IHHI', head, 4), (len(image), 2, 4, 512))
        self.assertEqual(MODULE.crc16(image[64:224]), struct.unpack_from('<H', head, 2)[0])
        for entry_index, expected_kind in ((0, 0), (1, 100)):
            entry = MODULE.header_codec(image[64 + entry_index * 80:144 + entry_index * 80])
            self.assertEqual(struct.unpack_from('<H', entry)[0], expected_kind)
            offset, size, stored = struct.unpack_from('<III', entry, 8)
            self.assertLessEqual(offset + stored, len(image))
            self.assertEqual(MODULE.crc16(image[offset:offset + size]), struct.unpack_from('<H', entry, 4)[0])
        self.assertEqual(image[1024:1024 + MODULE.FLASH_PREFIX], self.changed[:MODULE.FLASH_PREFIX])
        self.assertFalse(manifest['hardwareQualified'])
        self.assertEqual(manifest['preservation'], 'unit-baseline-app-only')

    def test_known_crc_vector(self):
        self.assertEqual(MODULE.crc16(b'123456789'), 0x31c3)

    def test_every_protected_boundary_rejects(self):
        with patch.object(MODULE, 'LOADER_SHA256', self.loader_digest):
            for offset in (0, 0x3fff, 0x4040, MODULE.APP_START - 1, MODULE.APP_END, 0xfffff):
                candidate = bytearray(self.changed)
                candidate[offset] ^= 1
                with self.subTest(offset=offset), self.assertRaisesRegex(ValueError, 'outside'):
                    self.make(bytes(candidate))

    def test_no_change_and_stale_hashes_reject(self):
        with patch.object(MODULE, 'LOADER_SHA256', self.loader_digest):
            for options in ({'baseline_sha256': '0' * 64}, {'candidate_sha256': '0' * 64},
                            {'target_build_sha256': 'AB' * 32}, {'target_build_sha256': '0' * 64},
                            {'target_identity': 'FM-1_015'}):
                with self.subTest(options=options), self.assertRaises(ValueError):
                    self.make(**options)
            with self.assertRaisesRegex(ValueError, 'directory change'):
                self.make(self.base)

    def test_real_loader_pin_is_enforced(self):
        with self.assertRaisesRegex(ValueError, 'pinned official'):
            self.make()


if __name__ == '__main__':
    unittest.main()
