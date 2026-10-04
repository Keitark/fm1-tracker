"""Private stock-reference verification; no device I/O."""
from pathlib import Path
import hashlib
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import verify_stock_power as stock


class StockPowerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not stock.STOCK.is_file():
            raise unittest.SkipTest('Private stock 010 reference not available')
        cls.blob = stock.STOCK.read_bytes()

    def test_pinned_evidence(self):
        result = stock.verify(self.blob)
        self.assertEqual(result['instruction_points_verified'], 12)
        self.assertEqual(result['effective_parameters']['vdc14_lev'], 7)
        self.assertFalse(result['hardware_voltage_measurement'])

    def test_different_stock_rejected(self):
        with self.assertRaisesRegex(ValueError, 'hash'):
            stock.verify(self.blob[:-1])

    def test_changed_instruction_rejected_even_if_hash_reapproved(self):
        for address, _, label in stock.EVIDENCE:
            with self.subTest(label=label):
                blob = bytearray(self.blob)
                blob[address-stock.XIP] ^= 1
                with patch.object(stock, 'SHA256', hashlib.sha256(blob).hexdigest()):
                    with self.assertRaisesRegex(ValueError, 'instruction mismatch'):
                        stock.verify(blob)


if __name__ == '__main__':
    unittest.main()
