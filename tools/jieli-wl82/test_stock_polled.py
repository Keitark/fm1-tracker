"""Offline patch and fake-device tests only. Never access USB."""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import deploy_stock_polled_via_cli as mod


class StockPolled(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source, cls.target, cls.sectors = mod.validate_inputs()

    def test_exact_patch_and_all_mutations(self):
        _, white, _ = mod.build.marker.build()
        _, app, _ = mod.build.build()
        mod.build.validate_application(white, app)
        offset = mod.build.PATCH-mod.build.marker.XIP
        for address in [0, len(app)-1, *range(offset, offset+46)]:
            broken = bytearray(app); broken[address] ^= 1
            with self.assertRaises(ValueError):
                mod.build.validate_application(white, bytes(broken))
        self.assertEqual(hashlib.sha256(app).hexdigest(), mod.APP_SHA)

    def test_compiled_bytes_and_pixel_loop_contract(self):
        # Decode the exact supported instruction set, not a second C loop.
        code = mod.build.AFTER
        self.assertEqual((mod.IMAGE.parent/'pixels.bin').read_bytes(), code)
        pc = count = pixels = clears = 0
        initial_out = 0xdead00ff
        out = initial_out
        r4 = None
        while pc < len(code):
            op = code[pc:pc+4]
            if op == bytes.fromhex('00 ef 80 0f'): out |= 0x100; pc += 4
            elif op == bytes.fromhex('c0 ef 80 00'): out &= ~0x80; pc += 4
            elif op == bytes.fromhex('60 e0 e1 3b'): count = 0x1c200; pc += 4
            elif op == bytes.fromhex('00 ef 80 1c'): clears += 1; pc += 4
            elif op == bytes.fromhex('42 ea ff 10'):
                self.assertEqual(out & 0x180, 0x100)
                pixels += 1; pc += 4
            elif code[pc:pc+2] == bytes.fromhex('14 60'): r4 = 0x8000; pc += 2
            elif op == bytes.fromhex('54 e8 fd 79'):
                pc += 4 if r4 & 0x8000 else -2
            elif code[pc:pc+2] == bytes.fromhex('fb 3f'): count -= 1; pc += 2
            elif code[pc:pc+2] == bytes.fromhex('f3 57'): pc += -16 if count else 2
            elif op == bytes.fromhex('00 ef 80 00'): out |= 0x80; pc += 4
            elif code[pc:pc+2] == bytes.fromhex('04 60'): r4 = out; pc += 2
            elif code[pc:pc+2] == b'\0\0': pc += 2
            else: self.fail('Unknown opcode at '+hex(pc))
            self.assertLessEqual(pixels, 115200)
        self.assertEqual(pixels, 115200)
        self.assertEqual(clears, 115201)
        self.assertEqual(r4, initial_out | 0x180)
        self.assertEqual(count, 0)

    def simulate(self, fail=None):
        memory = bytearray(self.source); writes = []; full = []
        def cli(device, commands, log):
            for command in commands:
                verb, rest = command.split(' ', 1)
                if verb == 'write':
                    address, path = rest.split(' ', 1); address = int(address)
                    self.assertIn(address, self.sectors)
                    data = Path(path).read_bytes(); self.assertEqual(len(data), 4096)
                    writes.append(address)
                    if fail == 'transport': raise IOError('Unknown write outcome')
                    memory[address:address+4096] = data
                elif verb == 'read':
                    self.assertTrue(writes, 'Pre-write read forbidden')
                    address, size, path = rest.split(' ', 2); address, size = int(address), int(size)
                    if size == 1048576: full.append(len(writes))
                    data = bytes(memory[address:address+size])
                    if fail == 'sector' or (fail == 'final' and full): data = bytes([data[0]^1])+data[1:]
                    Path(path).write_bytes(data)
                else: self.fail('Unexpected IO command')
        with tempfile.TemporaryDirectory() as folder, contextlib.redirect_stdout(io.StringIO()):
            folder = Path(folder); saved = folder/'saved.bin'; saved.write_bytes(self.source)
            if fail:
                with self.assertRaises((ValueError, IOError)):
                    mod.deployment.deploy('fake', folder, self.source, self.target, cli,
                        self.sectors, stock_power=True, verified_preflight=saved)
            else:
                mod.deployment.deploy('fake', folder, self.source, self.target, cli,
                    self.sectors, stock_power=True, verified_preflight=saved)
            report = json.loads((folder/'deployment.json').read_text())
        return memory, writes, full, report

    def test_fake_forward_no_prewrite_reads(self):
        memory, writes, full, report = self.simulate()
        self.assertEqual(memory, self.target)
        self.assertEqual(writes, mod.SECTORS)
        self.assertEqual(full, [45])
        self.assertEqual(report['status'], 'written_and_readback_verified')
        self.assertFalse(report['automatic_reset'])

    def test_fail_stop(self):
        for failure, count in (('sector', 1), ('transport', 1), ('final', 45)):
            _, writes, _, report = self.simulate(failure)
            self.assertEqual(len(writes), count)
            self.assertEqual(report['status'], 'failed')

    def test_bad_images_and_scope_rejected(self):
        for field in ('SOURCE_SHA', 'IMAGE_SHA', 'APP_SHA'):
            with patch.object(mod, field, '0'*64), self.assertRaises(ValueError): mod.validate_inputs()
        for sectors in (self.sectors[:-1], sorted(self.sectors), self.sectors+[0xee000]):
            with tempfile.TemporaryDirectory() as folder, self.assertRaises(ValueError):
                mod.deployment.deploy('fake', Path(folder), self.source, self.target,
                    lambda *args: self.fail('Unexpected IO'), sectors, stock_power=True)
        with tempfile.TemporaryDirectory() as folder:
            bad = Path(folder)/'bad.bin'; bad.write_bytes(self.target)
            with self.assertRaises(ValueError):
                mod.deployment.deploy('fake', Path(folder), self.source, self.target,
                    lambda *args: self.fail('Unexpected IO'), self.sectors,
                    stock_power=True, verified_preflight=bad)

    def test_existing_writer(self):
        with patch.object(mod.deployment, 'cli_call') as call:
            mod.call('fake', ['write 20480 sector.bin'], Path('fake.log'))
            call.assert_called_once_with('fake', ['write 20480 sector.bin'], Path('fake.log'), stock_power=True)


if __name__ == '__main__': unittest.main(verbosity=2)
