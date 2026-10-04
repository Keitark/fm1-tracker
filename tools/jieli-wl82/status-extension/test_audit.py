"""Offline regression checks; run after build.py. No hardware operations."""
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
from audit import verify
import audit_wl82_loader as loader


class ArtifactAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        out = loader.ROOT / 'tmp/wl82-status-extension'
        cls.extension = (out / 'extension.bin').read_bytes()
        cls.hook = (out / 'hook.bin').read_bytes()
        cls.original = loader.decode(loader.SOURCE.read_bytes())

    def test_reviewed_outputs(self):
        result = verify(self.extension, self.hook, self.original)
        self.assertTrue(result['exact_reviewed_machine_code'])
        self.assertFalse(result['hardware_qualified'])

    def test_every_extension_byte_is_bound(self):
        for index in range(len(self.extension)):
            changed = bytearray(self.extension)
            changed[index] ^= 1
            with self.subTest(index=index), self.assertRaises(ValueError):
                verify(changed, self.hook, self.original)

    def test_every_hook_byte_is_bound(self):
        for index in range(len(self.hook)):
            changed = bytearray(self.hook)
            changed[index] ^= 1
            with self.subTest(index=index), self.assertRaises(ValueError):
                verify(self.extension, changed, self.original)

    def test_changed_original_rejected(self):
        changed = bytearray(self.original)
        changed[0] ^= 1
        with self.assertRaises(ValueError):
            verify(self.extension, self.hook, changed)

    def test_unresolved_and_wrong_sizes_rejected(self):
        with self.assertRaises(ValueError):
            verify(self.extension, self.hook, self.original, ' U missing_symbol')
        for extension in (self.extension[:-1], self.extension + b'\0'):
            with self.assertRaises(ValueError):
                verify(extension, self.hook, self.original)


if __name__ == '__main__':
    unittest.main()
