"""Offline tests of the pinned private backup; never print its contents."""
import hashlib
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tools/jieli-wl82'))
import audit_stock_bootloader as boot


class StockBootloaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path=ROOT/'private-backups/readback-20260926-024637/fm1-flash-1MiB.bin'
        if not path.is_file():raise unittest.SkipTest('Private backup not present')
        cls.raw=path.read_bytes()

    def test_pinned_decode_and_handoff(self):
        code,report=boot.decode(self.raw)
        self.assertEqual(len(code),14368)
        self.assertEqual(report['code_sha256'],'fcaf033c5e10526353ecb11279a7556ac8645026d370c0267912cb4bfa7c7e4d')
        self.assertEqual(report['handoff_evidence']['prefix_bytes_copied'],24)
        self.assertFalse(report['config_or_reserved_extracted'])
        self.assertFalse(report['flashable'])

    def test_unreviewed_backup_rejected(self):
        with self.assertRaisesRegex(ValueError,'Unreviewed stock backup'):
            boot.decode(self.raw[:-1])

    def test_crc_layers_reject_corruption(self):
        for offset,reason in ((34,'directory CRC'),(0xa2,'bank header or CRC'),(0xb2,'data CRC')):
            with self.subTest(offset=offset):
                raw=bytearray(self.raw);raw[offset]^=1
                # Test only: allow the mutated hash to reach the independent
                # CRC checks. Production always enforces the fixed full hash.
                with patch.object(boot,'BACKUP_SHA',hashlib.sha256(raw).hexdigest()):
                    with self.assertRaisesRegex(ValueError,reason):boot.decode(raw)

    def test_handoff_opcode_change_rejected(self):
        code,_=boot.decode(self.raw);code=bytearray(code)
        code[0x1c051da-0x1c02000]^=1
        with self.assertRaisesRegex(ValueError,'handoff instruction'):
            boot.inspect_handoff(code)


if __name__=='__main__':unittest.main()
