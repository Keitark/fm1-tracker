"""Offline regression tests for guards in the existing Jieli protocol tool."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'jl-uboot-tool'))
from jltech.uboot import JL_LoaderV2
from jltech.crc import jl_crc16
from scsiio.common import SCSIException


class Transport:
    def __init__(self, failure=None):
        self.failure = failure
        self.calls = []

    def execute(self, cdb, outgoing, incoming):
        self.calls.append((bytes(cdb), outgoing))
        if incoming is not None:
            if cdb[:2] == b'\xfd\x05':
                incoming[:] = bytes([0x5a]) * len(incoming)
            else:
                incoming[:] = cdb[:2] + bytes(14)
                if self.failure == 'echo':
                    incoming[0] ^= 1
                if self.failure == 'erase':
                    incoming[2] = 1
            if self.failure == 'length':
                del incoming[-1:]
        return 1 if self.failure == 'residue' else 0


class ProtocolTests(unittest.TestCase):
    def test_sector_erase_encoding(self):
        dev = Transport()
        JL_LoaderV2(dev).flash_erase_sector(0x5000)
        self.assertEqual(dev.calls, [(b'\xfb\x01\x00\x00\x50\x00' + b'\xff'*10, None)])

    def test_erase_failures_stop_without_retry(self):
        for failure in ('echo', 'erase', 'length', 'residue'):
            with self.subTest(failure=failure):
                dev = Transport(failure)
                with self.assertRaises(SCSIException):
                    JL_LoaderV2(dev).flash_erase_sector(0x5000)
                self.assertEqual(len(dev.calls), 1)

    def test_write_encoding_and_short_transfer(self):
        payload = bytes(range(256))
        for failure in (None, 'residue'):
            dev = Transport(failure)
            protocol = JL_LoaderV2(dev)
            if failure:
                with self.assertRaises(SCSIException):
                    protocol.flash_write(0x5000, payload)
            else:
                protocol.flash_write(0x5000, payload)
            expected = b'\xfb\x04\x00\x00\x50\x00\x01\x00\x00' + jl_crc16(payload).to_bytes(2, 'little') + b'\xff'*5
            self.assertEqual(dev.calls, [(expected, payload)])

    def test_read_exact_or_fail(self):
        for failure in (None, 'length', 'residue'):
            dev = Transport(failure)
            protocol = JL_LoaderV2(dev)
            if failure:
                with self.assertRaises(SCSIException):
                    protocol.flash_read(0x5000, 256)
            else:
                self.assertEqual(protocol.flash_read(0x5000, 256), bytes([0x5a])*256)
            self.assertEqual(dev.calls, [(b'\xfd\x05\x00\x00\x50\x00\x01\x00' + b'\xff'*8, None)])


if __name__ == '__main__':
    unittest.main()
