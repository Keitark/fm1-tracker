"""Offline patch and fake-device deployment tests; no USB IO."""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import deploy_stock_marker_via_cli as mod


class MarkerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.forward = mod.validate_inputs()
        cls.reverse = mod.validate_inputs(True)

    def test_exact_stock_patch_and_pinned_output(self):
        target, app, report = mod.marker.build()
        self.assertEqual(hashlib.sha256(target).hexdigest(),
                         'fcc6ef41fc48f5e89904c184af6f5ffa798e67ad2bb3cadf33d1c7905b562a69')
        self.assertTrue(all(report['checks'].values()))
        self.assertEqual(report['changed_application_bytes'], 2)
        self.assertEqual(report['changed_sector_offsets'], ['0x4000', '0x25000'])
        original, _, _ = mod.marker.decode_application(mod.deployment.BASELINE.read_bytes())
        for offset in (0, mod.marker.PATCH-mod.marker.XIP-1,
                       mod.marker.PATCH-mod.marker.XIP, mod.marker.PATCH-mod.marker.XIP+2,
                       len(app)-1):
            bad = bytearray(app)
            bad[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                mod.marker.validate_application(original, bad)

    def simulate(self, reverse=False, fail=None, reuse=False):
        source, target, sectors = self.reverse if reverse else self.forward
        memory = bytearray(source)
        writes, full = [], []
        def cli(device, commands, log):
            for command in commands:
                verb, rest = command.split(' ', 1)
                if verb == 'read':
                    if reuse:
                        self.assertTrue(writes, 'Read occurred before first write')
                        self.assertNotIn('preflight-', rest)
                        self.assertNotIn('before-', rest)
                    address, size, path = rest.split(' ', 2)
                    address, size = int(address), int(size)
                    if size == 1048576:
                        full.append(len(writes))
                    data = bytes(memory[address:address+size])
                    if ((fail == 'preflight' and len(full) == 2) or
                        (fail == 'sector' and writes) or
                        (fail == 'final' and len(full) == 3)):
                        data = bytes([data[0]^1])+data[1:]
                    Path(path).write_bytes(data)
                elif verb == 'write':
                    address, path = rest.split(' ', 1)
                    address = int(address)
                    self.assertIn(address, sectors)
                    data = Path(path).read_bytes()
                    self.assertEqual(len(data), 4096)
                    writes.append(address)
                    if fail == 'transport':
                        raise IOError('Unknown write result')
                    memory[address:address+4096] = data
                else:
                    self.fail('Unexpected device command')
        with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stdout(io.StringIO()):
            completed = Path(directory)/'completed.bin' if reuse else None
            if completed:
                completed.write_bytes(source)
            if fail:
                with self.assertRaises((ValueError, IOError)):
                    mod.deployment.deploy('fake', Path(directory), source, target, cli, sectors, stock_power=True,
                                          verified_preflight=completed)
            else:
                mod.deployment.deploy('fake', Path(directory), source, target, cli, sectors, stock_power=True,
                                      verified_preflight=completed)
            receipt = json.loads((Path(directory)/'deployment.json').read_text())
        return memory, target, sectors, writes, full, receipt

    def test_forward_and_stock_restore(self):
        for reverse in (False, True):
            memory, target, sectors, writes, full, receipt = self.simulate(reverse)
            self.assertEqual(memory, target)
            self.assertEqual(writes, sectors)
            self.assertEqual(full, [0, 0, len(sectors)])
            self.assertEqual(receipt['status'], 'written_and_readback_verified')
            self.assertFalse(receipt['automatic_reset'])
            self.assertFalse(receipt['hardware_boot_verified'])

    def test_failure_stops_without_retry(self):
        for mode, count in (('preflight', 0), ('sector', 1), ('transport', 1), ('final', 45)):
            with self.subTest(mode=mode):
                *_, writes, full, receipt = self.simulate(fail=mode)
                self.assertEqual(len(writes), count)
                self.assertEqual(receipt['status'], 'failed')

    def test_reuse_completed_read_skips_all_prewrite_reads(self):
        memory, target, sectors, writes, full, receipt = self.simulate(reuse=True)
        self.assertEqual(memory, target)
        self.assertEqual(writes, sectors)
        self.assertEqual(full, [len(sectors)])
        self.assertEqual(receipt['preflight_mode'], 'reuse_completed_read_no_further_prewrite_reads')

    def test_bad_completed_read_rejected_before_io(self):
        source, target, sectors = self.forward
        with tempfile.TemporaryDirectory() as directory:
            bad = Path(directory)/'bad.bin'
            bad.write_bytes(b'wrong')
            with self.assertRaises(ValueError):
                mod.deployment.deploy('fake', Path(directory), source, target,
                    lambda *a: self.fail('IO before validation'), sectors, stock_power=True,
                    verified_preflight=bad)

    def test_out_of_scope_rejected_before_io(self):
        source, target, sectors = self.forward
        for bad in (sectors[:-1], sectors+[0x31000], sectors+[0xee000], sorted(sectors)):
            with tempfile.TemporaryDirectory() as directory, self.assertRaises(ValueError):
                mod.deployment.deploy('fake', Path(directory), source, target,
                    lambda *a: self.fail('IO before validation'), bad, stock_power=True)

    def test_existing_writer_used(self):
        with patch.object(mod.deployment, 'cli_call') as cli:
            mod.call('fake', ['read 0 256 x.bin'], Path('x.log'))
            cli.assert_called_once_with('fake', ['read 0 256 x.bin'], Path('x.log'), stock_power=True)

    def test_wrong_source_rejected(self):
        with patch.object(mod, 'PREVIOUS_SHA', '0'*64), self.assertRaises(ValueError):
            mod.validate_inputs()


if __name__ == '__main__':
    unittest.main(verbosity=2)
