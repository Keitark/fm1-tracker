"""Host transfer contract checks without opening a COM port."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zlib

CLIENT = Path(__file__).resolve().parents[1] / 'client.py'
spec = importlib.util.spec_from_file_location('fm1_mod_client', CLIENT)
client = importlib.util.module_from_spec(spec)
spec.loader.exec_module(client)


class FakePort:
    def __init__(self, payload):
        self.payload = payload
        self.commands = []
        self.saved = False

    def exchange(self, port, command):
        del port
        self.commands.append(command)
        if command == 'MOD STOP':
            return 'OK MOD QUEUED'
        if command == 'MOD STATUS':
            return 'MOD status ready=1 running=0 dirty=1'
        if command == 'MOD INFO':
            return f'MOD INFO {len(self.payload):08x} {zlib.crc32(self.payload):08x} 1'
        if command.startswith('MOD READ '):
            _, _, offset, count = command.split()
            off, n = int(offset, 16), int(count, 16)
            return f'MOD DATA {off:08x} {self.payload[off:off+n].hex()}'
        if command.startswith('MOD SAVED '):
            self.saved = True
            return 'OK MOD SAVED'
        if command == 'MOD ABORT':
            return 'OK MOD ABORT'
        raise AssertionError(command)


class ClientTests(unittest.TestCase):
    def setUp(self):
        self.payload = bytes((i * 13) & 255 for i in range(2117))
        self.fake = FakePort(self.payload)
        self.patch = patch.object(client, 'exchange', side_effect=self.fake.exchange)
        self.patch.start()

    def tearDown(self):
        self.patch.stop()

    def test_save_round_trip_and_acknowledge_after_write(self):
        with tempfile.TemporaryDirectory() as root:
            destination = Path(root) / 'edit.mod'
            size, crc = client.save(None, destination)
            self.assertEqual(size, len(self.payload))
            self.assertEqual(crc, zlib.crc32(self.payload))
            self.assertEqual(destination.read_bytes(), self.payload)
            self.assertTrue(self.fake.saved)
            self.assertTrue(self.fake.commands[-1].startswith('MOD SAVED '))

    def test_existing_file_is_preserved(self):
        with tempfile.TemporaryDirectory() as root:
            destination = Path(root) / 'edit.mod'
            destination.write_bytes(b'original')
            with self.assertRaises(FileExistsError):
                client.save(None, destination)
            self.assertEqual(destination.read_bytes(), b'original')
            self.assertEqual(self.fake.commands, [])

    def test_bad_crc_does_not_create_output(self):
        original = self.fake.exchange
        mocked = client.exchange

        def bad_info(port, command):
            if command == 'MOD INFO':
                return f'MOD INFO {len(self.payload):08x} 00000000 1'
            return original(port, command)

        client.exchange = bad_info
        try:
            with tempfile.TemporaryDirectory() as root:
                destination = Path(root) / 'edit.mod'
                with self.assertRaisesRegex(RuntimeError, 'CRC mismatch'):
                    client.save(None, destination)
                self.assertFalse(destination.exists())
                self.assertIn('MOD ABORT', self.fake.commands)
        finally:
            client.exchange = mocked


if __name__ == '__main__':
    unittest.main()
