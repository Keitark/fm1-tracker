"""Pinned package and fake-CLI fault tests; no physical device operations."""
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import deploy_screen_first_via_cli as screen


class ScreenFirstTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.forward = screen.validate_inputs()
        cls.reverse = screen.validate_inputs(True)

    def simulate(self, reverse=False, fail=None):
        source, target, sectors = self.reverse if reverse else self.forward
        memory = bytearray(source)
        writes, full = [], []
        def cli(device, commands, log):
            for command in commands:
                verb, rest = command.split(' ', 1)
                if verb == 'read':
                    address, size, path = rest.split(' ', 2)
                    address, size = int(address), int(size)
                    if size == 1048576:
                        full.append(len(writes))
                    data = bytes(memory[address:address+size])
                    if ((fail == 'preflight' and len(full) == 2) or
                        (fail == 'sector' and writes) or
                        (fail == 'final' and len(full) == 3)):
                        data = bytes([data[0] ^ 1]) + data[1:]
                    Path(path).write_bytes(data)
                elif verb == 'write':
                    address, path = rest.split(' ', 1)
                    address = int(address)
                    payload = Path(path).read_bytes()
                    self.assertEqual(len(payload), 4096)
                    self.assertIn(address, screen.SECTORS)
                    writes.append(address)
                    if fail == 'transport':
                        raise IOError('Unknown write outcome')
                    memory[address:address+4096] = payload
                else:
                    self.fail('Forbidden CLI command')
        with tempfile.TemporaryDirectory() as folder, contextlib.redirect_stdout(io.StringIO()):
            if fail:
                with self.assertRaises((ValueError, IOError)):
                    screen.deployment.deploy('fake', Path(folder), source, target, cli, sectors, stock_power=True)
            else:
                screen.deployment.deploy('fake', Path(folder), source, target, cli, sectors, stock_power=True)
            report = json.loads((Path(folder)/'deployment.json').read_text())
        return memory, target, writes, full, report

    def test_forward_and_restore(self):
        for reverse in (False, True):
            memory, target, writes, full, report = self.simulate(reverse)
            self.assertEqual(memory, target)
            self.assertEqual(writes, screen.SECTORS)
            self.assertEqual(full, [0, 0, 45])
            self.assertEqual(report['status'], 'written_and_readback_verified')
            self.assertFalse(report['hardware_boot_verified'])

    def test_failures_stop_without_retry(self):
        for failure, count in (('preflight', 0), ('sector', 1), ('transport', 1), ('final', 45)):
            with self.subTest(failure=failure):
                _, _, writes, _, report = self.simulate(fail=failure)
                self.assertEqual(len(writes), count)
                self.assertEqual(report['status'], 'failed')

    def test_cli_uses_existing_writer_and_bounds(self):
        with patch.object(screen.deployment, 'cli_call') as cli:
            screen.call('fake', ['read 0 256 result.bin'], Path('fake.log'))
            cli.assert_called_once_with('fake', ['read 0 256 result.bin'], Path('fake.log'), stock_power=True)

    def test_bad_scope_rejected_before_io(self):
        source, target, sectors = self.forward
        for bad in (sectors[:-1], sectors+[0x31000], sectors+[0xee000], sectors+sectors[:1], sorted(sectors)):
            with tempfile.TemporaryDirectory() as folder, self.assertRaises(ValueError):
                screen.deployment.deploy('fake', Path(folder), source, target,
                    lambda *a: self.fail('Device access before rejection'), bad, stock_power=True)

    def test_artifact_pin_rejected(self):
        with patch.object(screen, 'APP_SHA', '0'*64), self.assertRaisesRegex(ValueError, 'Unreviewed'):
            screen.validate_inputs()


if __name__ == '__main__':
    unittest.main()
